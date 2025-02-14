#ifndef LISTS_H
#define LISTS_H
#include <string>
#include <vector>
#include "Consts.h"
#include "types.h"
#include <mpi.h>
#include "Things.h"

typedef double (*DerivedFunction)(double);
typedef void (*ObjectiveFunction)(double*, double*, double*);

class Model {
public:

    struct Setting : UnitThing {
        std::string defaultValue;
        bool isDerived;
        int derivedSetting;
        DerivedFunction derivedValue;
        inline Setting() : isDerived(false) {};
        inline Setting(const int& id_, const std::string& name_, const std::string& unit_, 
                const std::string& defaultValue_, const bool& isDerived_, const int& derivedSetting_, const DerivedFunction& derivedValue_)
            : UnitThing(id_,name_,unit_), defaultValue(defaultValue_), isDerived(isDerived_), derivedSetting(derivedSetting_), derivedValue(derivedValue_) {}
        inline Setting(const int& id_, const std::string& name_, const std::string& unit_, const std::string& defaultValue_)
            : UnitThing(id_,name_,unit_), defaultValue(defaultValue_), isDerived(false) {}
    };

    struct ZoneSetting : UnitThing {
        std::string defaultValue;
        inline ZoneSetting() {};
        inline ZoneSetting(const int& id_, const std::string& name_, const std::string& unit_, const std::string& defaultValue_)
            : UnitThing(id_,name_,unit_), defaultValue(defaultValue_) {}
    };

    struct Quantity : UnitThing {
        bool isVector;
        bool isAdjoint;
        inline Quantity() : isVector(false), isAdjoint(false) {};
        inline Quantity(const int& id_, const std::string& name_, const std::string& unit_, const bool& isVector_, const bool& isAdjoint_=false)
            : UnitThing(id_,name_,unit_), isVector(isVector_), isAdjoint(isAdjoint_) {}
    };

    struct NodeTypeFlag : Thing {
        big_flag_t flag;
        int group_id;
        big_flag_t group_flag;
        inline NodeTypeFlag() : flag(0), group_id(INVALID_ID), group_flag(0) {}
        inline NodeTypeFlag(const big_flag_t& flag_, const std::string& name_, const big_flag_t& group_flag_)
            : Thing(flag_,name_), flag(flag_), group_id(group_flag_), group_flag(group_flag_) {}
    };

    struct NodeTypeGroupFlag : Thing {
        big_flag_t flag;
        int shift;
        int max;
        int capacity;
        int bits;
        bool isSave;
        inline NodeTypeGroupFlag() : flag(0), shift(0), max(0), capacity(0), bits(0), isSave(false) {}
        inline NodeTypeGroupFlag(const big_flag_t& flag_, const std::string& name_,
                const int& shift_, const int& max_, const int& capacity_, const int& bits_, const bool& isSave_)
            : Thing(flag_,name_), flag(flag_), shift(shift_), max(max_), capacity(capacity_), bits(bits_), isSave(isSave_) {}
    };

    struct Global : UnitThing {
        MPI_Op operation;
        int inObjId;
        bool isAdjoint;
        inline Global() : operation(NULL), inObjId(INVALID_ID), isAdjoint(false) {}
        inline Global(const int& id_, const std::string& name_, const std::string& unit_,
                const MPI_Op& operation_, const bool& isAdjoint_=false)
            : UnitThing(id_,name_,unit_), operation(operation_), inObjId(id_ + IN_OBJ_OFFSET), isAdjoint(isAdjoint_) {}
    };

    struct Option : Thing {
        bool isActive;
        inline Option() : isActive(false) {}
        inline Option(const int& id_, const std::string& name_, const bool& isActive_)
            : Thing(id_,name_), isActive(isActive_) {}
    };

    struct Scale : UnitThing {
        using UnitThing::UnitThing;
    };

    struct Field : Thing {
        std::string niceName;
        bool isParameter;
        bool isAverage;
        int accessArea;
        bool simpleAccess;
        bool isAdjoint;
        std::string adjointName;
        std::string tangentName;
        inline Field() : niceName("invalid"), isParameter(false), isAverage(false), accessArea(0), simpleAccess(false),
            isAdjoint(false), adjointName("invalid"), tangentName("invalid") {}
        inline Field(const int& id_, const std::string& name_, const std::string& niceName_,
                const bool& isParameter_, const bool& isAverage_, const int& accessArea_, const bool& simpleAccess_)
            : Thing(id_,name_), niceName(niceName_)
            , isParameter(isParameter_), isAverage(isAverage_), accessArea(accessArea_), simpleAccess(simpleAccess_)
            , isAdjoint(false) {}
        inline Field(const int& id_, const std::string& name_, const std::string& niceName_,
                const bool& isParameter_, const bool& isAverage_, const int& accessArea_, const bool& simpleAccess_,
                const bool& isAdjoint_, const std::string& adjointName_, const std::string& tangentName_)
            : Thing(id_,name_), niceName(niceName_)
            , isParameter(isParameter_), isAverage(isAverage_), accessArea(accessArea_), simpleAccess(simpleAccess_)
            , isAdjoint(isAdjoint_), adjointName(adjointName_), tangentName(tangentName_) {}
    };

    struct Action : Thing {
        std::vector<int> stages;
        inline Action() {}
        inline Action(const int& id_, const std::string& name_, const std::vector<int>& stages_)
            : Thing(id_,name_), stages(stages_) {}
    };

    struct Stage : Thing {
        std::string mainFun;
        bool isParticle;
        bool isAdjoint;
        inline Stage() : mainFun("invalid"), isParticle(false), isAdjoint(false) {}
        inline Stage(const int& id_, const std::string& name_, const std::string& mainFun_, const bool& isParticle_, const bool& isAdjoint_ = false)
            : Thing(id_,name_), mainFun(mainFun_), isParticle(isParticle_), isAdjoint(isAdjoint_) {}
    };

    struct Objective : Thing {
        ObjectiveFunction fun;
        inline Objective() : fun(NULL) {}
        inline Objective(const int& id_, const std::string& name_, const ObjectiveFunction& fun_)
            : Thing(id_,name_), fun(fun_) {}
    };

    std::string name;
    typedef Things<Setting> Settings;
    Settings settings;
    typedef Things<ZoneSetting> ZoneSettings;
    ZoneSettings zonesettings;
    typedef Things<Quantity> Quantities;
    Quantities quantities;
    typedef Things<NodeTypeFlag> NodeTypeFlags;
    NodeTypeFlags nodetypeflags;
    typedef Things<NodeTypeGroupFlag> NodeTypeGroupFlags;
    NodeTypeGroupFlags nodetypegroupflags;
    typedef Things<Global> Globals;
    Globals globals;
    typedef Things<Option> Options;
    Options options;
    typedef Things<Scale> Scales;
    Scales scales;
    typedef Things<Field> Fields;
    Fields fields;
    typedef Things<Action> Actions;
    Actions actions;
    typedef Things<Stage> Stages;
    Stages stages;
    NodeTypeGroupFlag settingzones;
    typedef Things<Objective> Objectives;
    Objectives objectives;
};

class Model_m : public Model {
public:
    Model_m();
};


#endif
