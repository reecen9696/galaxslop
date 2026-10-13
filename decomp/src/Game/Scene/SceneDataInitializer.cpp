#ifdef TARGET_PC
#include "Game/Util/EventUtil.hpp"
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include "net/puppet/remote_player_puppet.hpp"
#include "net/world/world_sync.h"
#endif
#include "Game/Scene/SceneDataInitializer.hpp"
#include "Game/NameObj/NameObjArchiveListCollector.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Scene/StageDataHolder.hpp"
#include "Game/Scene/StageFileLoader.hpp"
#include "Game/Screen/LuigiLetter.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SceneUtil.hpp"

SceneDataInitializer::SceneDataInitializer() : NameObj("SceneDataInitializer") {
    mFileLoader = nullptr;
    mDataHolder = nullptr;
}

void SceneDataInitializer::startStageFileLoad() {
    mFileLoader = new StageFileLoader(MR::getCurrentStageName());
    mFileLoader->startLoadingStageFile();

    if (!MR::isStageDisablePauseMenu()) {
        MR::mountAsyncArchive("/LayoutData/PauseMenu.arc", MR::getAproposHeapForSceneArchive(0.03f));
    }
}

void SceneDataInitializer::startStageFileLoadAfterScenarioSelected() {
    NameObjArchiveListCollector collector;

    LuigiLetter::makeArchiveListForMenu(&collector, JMapInfoIter());
#ifdef TARGET_PC
    // Co-op world sync: the previous scene's actors are gone; this one's
    // register as they are placed (net/world).
    port_world_scene_reset();
    // Co-op: remote players are drawn with the player models (net/puppet).
    // The save's own character is stationed; load the other with the stage,
    // and every power-up form for both characters: a remote player can be
    // in a form the local stage places no item for (the local player gets
    // them too, for the F-key debug switch).  ~2.6 MB, so into the scene
    // heap rather than the stage's fixed 16 MB file cache, which they
    // overflow (OSPanic in a later stage file load).
    if (port_net_wants_puppets() && !MR::isStageFileSelect()) {
        const char* const coop[] = {MR::isPlayerLuigi() ? "Mario" : "Luigi", MR::isPlayerLuigi() ? "MarioFace" : "LuigiFace",
                                    "BeeMario", "BeeLuigi", "BeeMarioWing", "HopperMario", "HopperLuigi", "IceMario",
                                    "IceLuigi", "IceMarioHandL", "IceMarioHandR", "InvincibleMario", "InvincibleLuigi",
                                    "InvincibleMarioHandL", "InvincibleMarioHandR", "TeresaMario", "TeresaLuigi"};
        for (const char* name : coop) {
            MR::mountAsyncArchiveByObjectOrLayoutName(name, MR::getSceneHeapGDDR3());
        }
    }
#endif

    for (int i = 0; i < collector.getArchiveNum(); i++) {
        MR::mountAsyncArchiveByObjectOrLayoutName(collector.getArchive(i), nullptr);
    }
}

void SceneDataInitializer::waitDoneStageFileLoad() {
    mFileLoader->waitLoadedStageFile();
    mDataHolder = static_cast< StageDataHolder* >(MR::createSceneObj(SceneObj_StageDataHolder));
}

void SceneDataInitializer::startActorFileLoadCommon() {
    mDataHolder->requestFileLoadCommon();
}

void SceneDataInitializer::startActorFileLoadScenario() {
    mDataHolder->requestFileLoadScenario();
}

void SceneDataInitializer::startActorPlacement() {
    mDataHolder->initPlacement();
}

void SceneDataInitializer::initAfterScenarioSelected() {
    mDataHolder->initAfterScenarioSelected();
#ifdef TARGET_PC
    port_net_puppets_create_for_scene();  // co-op: remote players (net/puppet)
#endif
}

SceneDataInitializer::~SceneDataInitializer() {
}
