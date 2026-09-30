#include <AYAbility/Authoring.h>
#include <nlohmann/json.hpp>
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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AuthoringDocument,version,actions,equipment,
    skills,fireDeliveries,derivedSkills,effectReferences)

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
Result<std::shared_ptr<const Catalog>> compileAuthoring(const AuthoringDocument& document) {
    if(document.version!=1)return invalid<std::shared_ptr<const Catalog>>(
        "unsupported Ability authoring version");
    auto catalog=Catalog::compile(document.actions,document.equipment,document.skills);
    if(!catalog)return std::unexpected(catalog.error());
    std::set<Id> effects;
    for(auto id:document.effectReferences)
        if(!id || !effects.insert(id).second)
            return invalid<std::shared_ptr<const Catalog>>("duplicate or zero effect reference");
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
            for(const auto& stage:binding.definition.stages)
                if(stage.effect && !effects.contains(stage.effect))
                    return invalid<std::shared_ptr<const Catalog>>("unresolved Combat effect reference");
        }
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
