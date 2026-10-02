#include <AYAbility/Authoring.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <set>

namespace ayt::ability {
using nlohmann::json;
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Parameter,id,unit,value)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CurvePoint,level,value)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(NumericTrack,mode,constant,table,curve)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ParameterPatch,parameter,operation,values)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Feature,id,prerequisite,exclusiveGroup,unlockLevel)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ActionDefinition,id,behaviorId,defaults,features)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EquipmentActionConfig,id,action,parameters)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SkillCost,resource,amount)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SkillDefinition,id,action,kind,maxLevel,parameters,features,cost)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DerivedStage,id,kind,emitters,amountParameter,
    amount,recipe,hitMode,intervalTicks,lifetimeTicks,trigger,predecessor,delayTicks,
    emittersParameter,operation,intent,attribute,allowedTargets,effect)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DerivedDefinition,sourceLoss,stages)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DeliveryBinding,owner,definition)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Delivery2DProfile,velocity,spawnOffset,
    radius,layerMask,reverseAfterTicks,ignoreSource,obstacle,target)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Delivery2DBinding,action,stage,profile)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AreaSkill2DBinding,skill,effect,radius,
    offset,layerMask,includeSource,intent,allowed,requiredTag,requireSelected,
    selectionKey,maxCandidates,maxMembers,requireTargetAlive)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AuthoringDocument,version,actions,equipment,
    skills,fireDeliveries,derivedSkills,delivery2D,areaSkills2D,effectReferences)

namespace {
template<class T> Result<T> invalid(std::string message) {
    return std::unexpected(Diagnostic{Error::Invalid,std::move(message),{}});
}
bool parametersValid(const DerivedDefinition& definition,const ActionDefinition& action) {
    for(const auto& stage:definition.stages)
        for(auto [id,unit]:{std::pair{stage.amountParameter,Unit::Amount},
                            std::pair{stage.emittersParameter,Unit::Count}})
            if(id && std::none_of(action.defaults.begin(),action.defaults.end(),
                [&](const Parameter& parameter){return parameter.id==id && parameter.unit==unit;}))
                return false;
    return true;
}
}
Result<void> validateDelivery2DProfile(const Delivery2DProfile& profile) {
    for(auto value:profile.velocity)if(!std::isfinite(value))return invalid<void>("non-finite 2D velocity");
    for(auto value:profile.spawnOffset)if(!std::isfinite(value))return invalid<void>("non-finite 2D offset");
    if(profile.velocity[2]!=0 || profile.spawnOffset[2]!=0 ||
       !std::isfinite(profile.radius) || profile.radius<0 || !profile.layerMask ||
       profile.obstacle>CollisionResponse2D::PassThrough ||
       profile.target>CollisionResponse2D::PassThrough)
        return invalid<void>("invalid 2D delivery profile");
    return {};
}
Result<std::shared_ptr<const Catalog>> compileAuthoring(const AuthoringDocument& document) {
    if(document.version!=1)return invalid<std::shared_ptr<const Catalog>>(
        "unsupported Ability authoring version");
    auto catalog=Catalog::compile(document.actions,document.equipment,document.skills);
    if(!catalog)return std::unexpected(catalog.error());
    std::set<Id> effects;
    for(auto id:document.effectReferences)
        if(!id || !effects.insert(id).second)
            return invalid<std::shared_ptr<const Catalog>>("duplicate or zero effect reference");
    std::set<std::pair<Id,Id>> stageKeys;
    for(auto [fire,bindings]:{std::pair{true,&document.fireDeliveries},
                              std::pair{false,&document.derivedSkills}}) {
        std::set<Id> owners;
        for(const auto& binding:*bindings) {
            if(!binding.owner || !owners.insert(binding.owner).second)
                return invalid<std::shared_ptr<const Catalog>>("duplicate or zero delivery owner");
            if(auto valid=validateDerived(binding.definition);!valid)
                return std::unexpected(valid.error());
            const ActionDefinition* action=nullptr;
            if(fire) {
                auto config=(*catalog)->findEquipment(binding.owner);
                action=config?(*catalog)->findAction(config->action):nullptr;
                if(!action || action->behaviorId!=fire::Behavior)
                    return invalid<std::shared_ptr<const Catalog>>("fire delivery owner mismatch");
            } else {
                auto skill=(*catalog)->findSkill(binding.owner);
                action=skill?(*catalog)->findAction(skill->action):nullptr;
                if(!skill || skill->kind!=SkillKind::Active || !action ||
                   action->behaviorId!=derived::Behavior)
                    return invalid<std::shared_ptr<const Catalog>>("derived skill owner mismatch");
            }
            if(!parametersValid(binding.definition,*action))
                return invalid<std::shared_ptr<const Catalog>>("delivery parameter unit mismatch");
            for(const auto& stage:binding.definition.stages) {
                if(stage.effect && !effects.contains(stage.effect))
                    return invalid<std::shared_ptr<const Catalog>>("unresolved Combat effect reference");
                stageKeys.insert({action->id,stage.id});
            }
        }
    }
    std::set<std::pair<Id,Id>> profiles;
    for(const auto& binding:document.delivery2D) {
        const auto key=std::pair{binding.action,binding.stage};
        if(!stageKeys.contains(key) || !profiles.insert(key).second)
            return invalid<std::shared_ptr<const Catalog>>("2D profile references unknown or duplicate stage");
        if(auto valid=validateDelivery2DProfile(binding.profile);!valid)
            return std::unexpected(valid.error());
    }
    std::set<Id> areaSkills;
    for(const auto& binding:document.areaSkills2D) {
        if(!binding.skill || !areaSkills.insert(binding.skill).second ||
           !effects.contains(binding.effect))
            return invalid<std::shared_ptr<const Catalog>>(
                "duplicate area skill or unresolved Combat effect reference");
        const auto* skill=(*catalog)->findSkill(binding.skill);
        const auto* action=skill?(*catalog)->findAction(skill->action):nullptr;
        if(!skill || skill->kind!=SkillKind::Active || !action ||
           action->behaviorId!=area::Behavior)
            return invalid<std::shared_ptr<const Catalog>>("2D area owner mismatch");
        if(!std::isfinite(binding.radius) || binding.radius<=0 ||
           !std::isfinite(binding.offset[0]) || !std::isfinite(binding.offset[1]) ||
           !binding.layerMask || binding.intent>2 || !binding.allowed ||
           (binding.allowed&~std::uint32_t{63}) ||
           !binding.maxCandidates || binding.maxCandidates>65536 ||
           !binding.maxMembers || binding.maxMembers>65536 ||
           binding.requireSelected==binding.selectionKey.empty())
            return invalid<std::shared_ptr<const Catalog>>("invalid 2D area profile");
    }
    return catalog;
}
Result<AuthoringDocument> loadAuthoring(std::string_view source) {
    try {
        const auto value=json::parse(source.begin(),source.end());
        if(!value.is_object() || !value.contains("version"))
            return invalid<AuthoringDocument>("Ability authoring version missing");
        auto document=value.get<AuthoringDocument>();
        if(auto valid=compileAuthoring(document);!valid)return std::unexpected(valid.error());
        return document;
    } catch(const std::exception& error) {
        return invalid<AuthoringDocument>(error.what());
    }
}
Result<std::string> saveAuthoring(const AuthoringDocument& document) {
    if(auto valid=compileAuthoring(document);!valid)return std::unexpected(valid.error());
    try{return json(document).dump(2);}
    catch(const std::exception& error){return invalid<std::string>(error.what());}
}
} // namespace ayt::ability
