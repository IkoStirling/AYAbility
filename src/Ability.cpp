#include <AYAbility/Ability.h>
#include <limits>
#include <map>
#include <set>

namespace ayt::ability {
namespace {
template<class T> Result<T> fail(Error code, std::string message, std::string path={}) {
    return std::unexpected(Diagnostic{code,std::move(message),std::move(path)});
}
bool valid(Unit value) { return value>=Unit::Amount && value<=Unit::BasisPoints; }
bool valid(PatchOp value) { return value>=PatchOp::Replace && value<=PatchOp::MultiplyBasisPoints; }
Result<Value> add(Value a, Value b) {
    if((b>0 && a>std::numeric_limits<Value>::max()-b) ||
       (b<0 && a<std::numeric_limits<Value>::min()-b)) return fail<Value>(Error::Overflow,"parameter addition overflow");
    return a+b;
}
Result<Value> subtract(Value a, Value b) {
    if(b==std::numeric_limits<Value>::min()) {
        if(a>=0)return fail<Value>(Error::Overflow,"parameter subtraction overflow");
        return add(a,std::numeric_limits<Value>::max()).transform([](Value v){return v+1;});
    }
    return add(a,-b);
}
Result<Value> multiply(Value a, Value b) {
    if(!a || !b)return Value{0};
    if(a==-1 && b==std::numeric_limits<Value>::min())return fail<Value>(Error::Overflow,"parameter multiplication overflow");
    if(b==-1 && a==std::numeric_limits<Value>::min())return fail<Value>(Error::Overflow,"parameter multiplication overflow");
    if(a>0) {
        if(b>0 && a>std::numeric_limits<Value>::max()/b)return fail<Value>(Error::Overflow,"parameter multiplication overflow");
        if(b<0 && b<std::numeric_limits<Value>::min()/a)return fail<Value>(Error::Overflow,"parameter multiplication overflow");
    } else {
        if(b>0 && a<std::numeric_limits<Value>::min()/b)return fail<Value>(Error::Overflow,"parameter multiplication overflow");
        if(b<0 && a<std::numeric_limits<Value>::max()/b)return fail<Value>(Error::Overflow,"parameter multiplication overflow");
    }
    return a*b;
}
Result<void> validateTrack(const NumericTrack& track,std::uint32_t maxLevel) {
    if(!maxLevel || maxLevel>1024)return fail<void>(Error::Limit,"level limit");
    switch(track.mode) {
    case TrackMode::Constant:
        if(!track.table.empty() || !track.curve.empty())return fail<void>(Error::Invalid,"constant has extra points");
        return {};
    case TrackMode::Table:
        if(!track.curve.empty() || track.table.size()!=maxLevel)return fail<void>(Error::Invalid,"table must cover every level");
        return {};
    case TrackMode::Curve:
        if(!track.table.empty() || track.curve.empty() || track.curve.front().level!=1 ||
           track.curve.back().level!=maxLevel)return fail<void>(Error::Invalid,"curve must cover level range");
        for(std::size_t i=1;i<track.curve.size();++i)
            if(track.curve[i].level<=track.curve[i-1].level)return fail<void>(Error::Invalid,"curve levels must increase");
        return {};
    }
    return fail<void>(Error::Invalid,"unknown track mode");
}
Result<Value> sample(const NumericTrack& track,std::uint32_t level) {
    if(track.mode==TrackMode::Constant)return track.constant;
    if(track.mode==TrackMode::Table)return track.table[level-1];
    auto upper=std::lower_bound(track.curve.begin(),track.curve.end(),level,
        [](const CurvePoint& point,std::uint32_t requested){return point.level<requested;});
    if(upper==track.curve.end())return fail<Value>(Error::Invalid,"level beyond curve");
    if(upper->level==level)return upper->value;
    const auto& lower=*(upper-1);
    auto delta=subtract(upper->value,lower.value);if(!delta)return delta;
    auto product=multiply(*delta,static_cast<Value>(level-lower.level));if(!product)return product;
    return add(lower.value,*product/static_cast<Value>(upper->level-lower.level));
}
template<class T> bool sortedUnique(std::vector<T>& values) {
    std::sort(values.begin(),values.end(),[](const T& a,const T& b){return a.id<b.id;});
    return std::adjacent_find(values.begin(),values.end(),[](const T& a,const T& b){return a.id==b.id;})==values.end();
}
Result<void> validateFeatures(const std::vector<Feature>& features,std::uint32_t maxLevel,
                              const std::vector<Feature>& base={}) {
    std::set<Id> ids;
    std::map<Id,std::uint32_t> levels;
    for(const auto& feature:base){ids.insert(feature.id);levels.emplace(feature.id,feature.unlockLevel);}
    for(const auto& feature:features) {
        if(!feature.id || !feature.unlockLevel || feature.unlockLevel>maxLevel || !ids.insert(feature.id).second)
            return fail<void>(Error::Invalid,"invalid or duplicate feature");
        levels.emplace(feature.id,feature.unlockLevel);
    }
    for(const auto& feature:features) {
        if(feature.prerequisite && !ids.contains(feature.prerequisite))return fail<void>(Error::Missing,"feature dependency missing");
        if(feature.prerequisite==feature.id)return fail<void>(Error::Invalid,"feature depends on itself");
        if(feature.prerequisite && levels.at(feature.prerequisite)>feature.unlockLevel)
            return fail<void>(Error::Invalid,"feature prerequisite unlocks later");
        std::set<Id> visited;
        Id cursor=feature.id;
        while(cursor) {
            if(!visited.insert(cursor).second)return fail<void>(Error::Invalid,"feature dependency cycle");
            auto at=std::find_if(features.begin(),features.end(),[&](const Feature& item){return item.id==cursor;});
            if(at==features.end())break;
            cursor=at->prerequisite;
        }
    }
    return {};
}
Result<void> validatePatches(const std::vector<ParameterPatch>& patches,const ActionDefinition& action,
                             std::uint32_t maxLevel,bool equipment) {
    std::set<Id> ids;
    for(const auto& patch:patches) {
        if(!patch.parameter || !ids.insert(patch.parameter).second || !valid(patch.operation))
            return fail<void>(Error::Duplicate,"invalid or duplicate parameter patch");
        if(std::none_of(action.defaults.begin(),action.defaults.end(),[&](const Parameter& p){return p.id==patch.parameter;}))
            return fail<void>(Error::Missing,"parameter patch target missing");
        if(auto result=validateTrack(patch.values,maxLevel);!result)return result;
        if(equipment && patch.values.mode!=TrackMode::Constant)
            return fail<void>(Error::Invalid,"equipment parameter cannot depend on character level");
    }
    return {};
}
Result<void> applyPatches(std::vector<Parameter>& values,std::span<const ParameterPatch> patches,std::uint32_t level) {
    for(const auto& patch:patches) {
        auto at=std::find_if(values.begin(),values.end(),[&](const Parameter& p){return p.id==patch.parameter;});
        const auto operand=sample(patch.values,level);if(!operand)return std::unexpected(operand.error());
        Result<Value> next=at->value;
        switch(patch.operation) {
        case PatchOp::Replace: next=*operand;break;
        case PatchOp::Add: next=add(at->value,*operand);break;
        case PatchOp::MultiplyBasisPoints:
            if(*operand<0)return fail<void>(Error::Invalid,"negative basis-point factor");
            next=multiply(at->value,*operand).transform([](Value value){return value/10000;});break;
        }
        if(!next)return std::unexpected(next.error());
        at->value=*next;
    }
    return {};
}
} // namespace

Result<Value> ResolvedAction::value(Id parameter,Unit unit) const {
    auto at=std::lower_bound(parameters.begin(),parameters.end(),parameter,
        [](const Parameter& p,Id id){return p.id<id;});
    if(at==parameters.end() || at->id!=parameter)return fail<Value>(Error::Missing,"parameter missing");
    if(at->unit!=unit)return fail<Value>(Error::Invalid,"parameter unit mismatch");
    return at->value;
}
bool ResolvedAction::hasFeature(Id feature) const noexcept {
    return std::binary_search(features.begin(),features.end(),feature);
}
const ActionDefinition* Catalog::findAction(Id id) const noexcept {
    auto at=std::lower_bound(_actions.begin(),_actions.end(),id,[](const auto& item,Id key){return item.id<key;});
    return at!=_actions.end() && at->id==id?&*at:nullptr;
}
const EquipmentActionConfig* Catalog::findEquipment(Id id) const noexcept {
    auto at=std::lower_bound(_equipment.begin(),_equipment.end(),id,[](const auto& item,Id key){return item.id<key;});
    return at!=_equipment.end() && at->id==id?&*at:nullptr;
}
const SkillDefinition* Catalog::findSkill(Id id) const noexcept {
    auto at=std::lower_bound(_skills.begin(),_skills.end(),id,[](const auto& item,Id key){return item.id<key;});
    return at!=_skills.end() && at->id==id?&*at:nullptr;
}
Result<std::shared_ptr<const Catalog>> Catalog::compile(std::vector<ActionDefinition> actions,
        std::vector<EquipmentActionConfig> equipment,std::vector<SkillDefinition> skills) {
    if(actions.empty() || actions.size()>4096 || equipment.size()>65536 || skills.size()>65536)
        return fail<std::shared_ptr<const Catalog>>(Error::Limit,"catalog size limit");
    if(!sortedUnique(actions) || !sortedUnique(equipment) || !sortedUnique(skills))
        return fail<std::shared_ptr<const Catalog>>(Error::Duplicate,"duplicate definition identity");
    for(auto& action:actions) {
        if(!action.id || !action.behaviorId || !sortedUnique(action.defaults))
            return fail<std::shared_ptr<const Catalog>>(Error::Invalid,"invalid action identity or duplicate parameter");
        for(const auto& param:action.defaults)if(!param.id || !valid(param.unit))
            return fail<std::shared_ptr<const Catalog>>(Error::Invalid,"invalid action parameter");
        if(auto result=validateFeatures(action.features,1);!result)return std::unexpected(result.error());
    }
    auto catalog=std::shared_ptr<Catalog>(new Catalog);
    catalog->_actions=std::move(actions);
    for(auto& config:equipment) {
        if(!config.id || !config.action || !catalog->findAction(config.action))
            return fail<std::shared_ptr<const Catalog>>(Error::Missing,"equipment action missing");
        if(auto result=validatePatches(config.parameters,*catalog->findAction(config.action),1,true);!result)
            return std::unexpected(result.error());
    }
    for(auto& skill:skills) {
        if(!skill.id || !skill.action || !catalog->findAction(skill.action) ||
           skill.kind<SkillKind::Passive || skill.kind>SkillKind::Active ||
           !skill.maxLevel || skill.maxLevel>1024)
            return fail<std::shared_ptr<const Catalog>>(Error::Invalid,"invalid skill definition");
        const auto& action=*catalog->findAction(skill.action);
        if(auto result=validatePatches(skill.parameters,action,skill.maxLevel,false);!result)
            return std::unexpected(result.error());
        if(auto result=validateFeatures(skill.features,skill.maxLevel,action.features);!result)
            return std::unexpected(result.error());
        if(skill.cost) {
            if(skill.kind!=SkillKind::Active || skill.cost->resource.empty() ||
               !validateTrack(skill.cost->amount,skill.maxLevel))
                return fail<std::shared_ptr<const Catalog>>(Error::Invalid,"invalid active skill cost");
            for(std::uint32_t level=1;level<=skill.maxLevel;++level) {
                auto amount=sample(skill.cost->amount,level);
                if(!amount || *amount<0)return fail<std::shared_ptr<const Catalog>>(Error::Invalid,"negative or invalid skill cost");
            }
        }
    }
    catalog->_equipment=std::move(equipment);catalog->_skills=std::move(skills);
    return std::shared_ptr<const Catalog>(std::move(catalog));
}
Result<ResolvedAction> Catalog::resolve(Id actionId,Id equipmentConfig,
        std::span<const SkillLevel> levels,Id activeSkill) const {
    const auto* action=findAction(actionId);
    if(!action)return fail<ResolvedAction>(Error::Missing,"action missing");
    ResolvedAction result{.action=actionId,.behaviorId=action->behaviorId,
        .equipmentConfig=equipmentConfig,.activeSkill=activeSkill,.parameters=action->defaults};
    for(const auto& feature:action->features)result.features.push_back(feature.id);
    if(equipmentConfig) {
        const auto* config=findEquipment(equipmentConfig);
        if(!config || config->action!=actionId)return fail<ResolvedAction>(Error::Missing,"equipment configuration missing");
        if(auto applied=applyPatches(result.parameters,config->parameters,1);!applied)return std::unexpected(applied.error());
    }
    std::vector<SkillLevel> ordered(levels.begin(),levels.end());
    std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a.skill<b.skill;});
    if(std::adjacent_find(ordered.begin(),ordered.end(),[](auto a,auto b){return a.skill==b.skill;})!=ordered.end())
        return fail<ResolvedAction>(Error::Duplicate,"skill listed twice");
    std::vector<const Feature*> selectedFeatures;
    for(const auto& entry:ordered) {
        const auto* skill=findSkill(entry.skill);
        if(!skill || skill->action!=actionId || !entry.level || entry.level>skill->maxLevel)
            return fail<ResolvedAction>(Error::Invalid,"invalid skill level or action");
        if(skill->kind==SkillKind::Active && entry.skill!=activeSkill)continue;
        if(skill->kind==SkillKind::Passive && entry.skill==activeSkill)
            return fail<ResolvedAction>(Error::Invalid,"passive skill cannot be activated");
        if(auto applied=applyPatches(result.parameters,skill->parameters,entry.level);!applied)
            return std::unexpected(applied.error());
        for(const auto& feature:skill->features)if(entry.level>=feature.unlockLevel) {
            result.features.push_back(feature.id);selectedFeatures.push_back(&feature);
        }
        if(entry.skill==activeSkill && skill->cost) {
            auto amount=sample(skill->cost->amount,entry.level);
            if(!amount)return std::unexpected(amount.error());
            result.cost=std::pair{skill->cost->resource,*amount};
        }
    }
    if(activeSkill) {
        const auto* skill=findSkill(activeSkill);
        if(!skill || skill->kind!=SkillKind::Active ||
           std::none_of(ordered.begin(),ordered.end(),[&](auto item){return item.skill==activeSkill;}))
            return fail<ResolvedAction>(Error::Missing,"active skill not granted");
    }
    std::sort(result.features.begin(),result.features.end());
    if(std::adjacent_find(result.features.begin(),result.features.end())!=result.features.end())
        return fail<ResolvedAction>(Error::Duplicate,"feature unlocked twice");
    std::set<Id> groups;
    for(const auto* feature:selectedFeatures) {
        if(feature->prerequisite && !result.hasFeature(feature->prerequisite))
            return fail<ResolvedAction>(Error::Invalid,"feature dependency locked");
        if(feature->exclusiveGroup && !groups.insert(feature->exclusiveGroup).second)
            return fail<ResolvedAction>(Error::Invalid,"exclusive features unlocked together");
    }
    return result;
}
} // namespace ayt::ability
