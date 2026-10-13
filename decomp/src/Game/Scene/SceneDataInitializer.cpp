#ifdef TARGET_PC
#include "Game/Util/EventUtil.hpp"
#include "net/puppet/remote_player_puppet.hpp"
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
    // Co-op: remote players are drawn with the player models (net/puppet).
    // The save's own character is stationed; load the other with the stage.
    if (port_net_wants_puppets() && !MR::isStageFileSelect()) {
        collector.addArchive(MR::isPlayerLuigi() ? "Mario" : "Luigi");
        collector.addArchive(MR::isPlayerLuigi() ? "MarioFace" : "LuigiFace");
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
