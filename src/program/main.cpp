#include "lib.hpp"
#include "imgui_backend/imgui_impl_nvn.hpp"
#include "patches.hpp"
#include "fs.h"

#include <basis/seadRawPrint.h>
#include <prim/seadSafeString.h>
#include <resource/seadResourceMgr.h>
#include <filedevice/nin/seadNinSDFileDeviceNin.h>
#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>
#include <resource/seadArchiveRes.h>
#include <heap/seadHeapMgr.h>
#include <devenv/seadDebugFontMgrNvn.h>
#include <gfx/seadTextWriter.h>
#include <gfx/seadViewport.h>
#include <gfx/seadPrimitiveRenderer.h>

#include "rs/util.hpp"

#include "game/StageScene/StageScene.h"
#include "game/System/GameSystem.h"
#include "game/System/Application.h"
#include "game/HakoniwaSequence/HakoniwaSequence.h"
#include "game/Player/PlayerFunction.h"
#include "game/GameData/GameProgressData.h"

#include "al/util.hpp"
#include "al/util/LiveActorUtil.h"
#include "al/byaml/ByamlIter.h"
#include "al/fs/FileLoader.h"
#include "al/resource/Resource.h"
#include "al/collision/KCollisionServer.h"
#include "al/collision/alCollisionUtil.h"

#include "agl/utl.h"
#include "imgui_nvn.h"
#include "helpers/InputHelper.h"
#include "init.h"
#include "helpers/PlayerHelper.h"
#include "helpers.h"
#include "game/GameData/GameDataFunction.h"

#include "nerve/scene/NrvStageScene.h"

#include "statics.h"

#include <typeinfo>

static const char* DBG_FONT_PATH = "ImGuiData/Font/nvn_font_jis1.ntx";
static const char* DBG_SHADER_PATH = "ImGuiData/Font/nvn_font_shader_jis1.bin";
static const char* DBG_TBL_PATH = "ImGuiData/Font/nvn_font_jis1_tbl.bin";
static const char* DEV_WORLD_LIST_PATH = "LunaKitData/CustomWorldList";

#define IMGUI_ENABLED true

sead::TextWriter *gTextWriter;

void drawBackground() {

    agl::DrawContext *context = Application::instance()->mDrawInfo->mDrawContext;

    sead::Vector3<float> p1(-1, .3, 0); // top left
    sead::Vector3<float> p2(-.2, .3, 0); // top right
    sead::Vector3<float> p3(-1, -1, 0); // bottom left
    sead::Vector3<float> p4(-.2, -1, 0); // bottom right
    sead::Color4f c(.1, .1, .1, .9);

    agl::utl::DevTools::beginDrawImm(context, sead::Matrix34<float>::ident, sead::Matrix44<float>::ident);
    agl::utl::DevTools::drawTriangleImm(context, p1, p2, p3, c);
    agl::utl::DevTools::drawTriangleImm(context, p3, p4, p2, c);
}

void drawCursor() {
    agl::DrawContext *context = Application::instance()->mDrawInfo->mDrawContext;

    nn::hid::MouseState state{};
    nn::hid::GetMouseState(&state);
    agl::utl::DevTools::beginDrawImm(context, sead::Matrix34<float>::ident,
                                     sead::Matrix44<float>::ident);
    sead::Vector2f screenSize = sead::Vector2f(1280.0f, 720.0f) / 2.0f;
    agl::utl::DevTools::drawCursor(context, screenSize, sead::Vector2f(((float) state.x - screenSize.x) / screenSize.x,
                                                                       ((float) -state.y + screenSize.y) /
                                                                       screenSize.y), 0.5f);
}

void drawImGuiDebug() {

    auto bd = ImguiNvnBackend::getBackendData();
    ImGuiIO io = ImGui::GetIO();

    ImVec2 mousePos;
    InputHelper::getMouseCoords(&mousePos.x, &mousePos.y);
    ImVec2 scrollDelta;
    InputHelper::getScrollDelta(&scrollDelta.x, &scrollDelta.y);

    gTextWriter->setCursorFromTopLeft(sead::Vector2f(10.f, 480.f));

    nn::TimeSpan curTick = nn::os::GetSystemTick().ToTimeSpan();
    nn::TimeSpan prevTick(bd->lastTick);
    float sec = fabsf((double) (curTick - prevTick).GetNanoSeconds() / 1000000000.0);

    gTextWriter->printf("Delta Time: (u64) %llu (float) %f\n", sec, (float) sec);

    gTextWriter->printf("ImGui Delta Time: %f\n", io.DeltaTime);

    gTextWriter->printf("Mouse Coords: X: %f Y: %f\n", mousePos.x, mousePos.y);
    gTextWriter->printf("Scroll Delta: X: %f Y: %f\n", scrollDelta.x, scrollDelta.y);
}

void graNoclipCode(al::LiveActor *player) {

    static bool isFirst = true;

    static float speed = 25.0f;
    static float speedMax = 150.0f;
    static float vspeed = 20.0f;
    static float speedGain = 0.0f;

    sead::Vector3f *playerPos = al::getTransPtr(player);
    sead::Vector3f *cameraPos = al::getCameraPos(player, 0);
    sead::Vector2f *leftStick = al::getLeftStick(-1);

    // Its better to do this here because loading zones reset this.
    al::offCollide(player);
    al::setVelocityZero(player);

    // Mario slightly goes down even when velocity is 0. This is a hacky fix for that.
    playerPos->y += 1.4553f;

    float d = sqrt(al::powerIn(playerPos->x - cameraPos->x, 2) + (al::powerIn(playerPos->z - cameraPos->z, 2)));
    float vx = ((speed + speedGain) / d) * (playerPos->x - cameraPos->x);
    float vz = ((speed + speedGain) / d) * (playerPos->z - cameraPos->z);

    if (!al::isPadHoldZR(-1)) {
        playerPos->x -= leftStick->x * vz;
        playerPos->z += leftStick->x * vx;

        playerPos->x += leftStick->y * vx;
        playerPos->z += leftStick->y * vz;

        if (al::isPadHoldX(-1)) speedGain -= 0.5f;
        if (al::isPadHoldY(-1)) speedGain += 0.5f;
        if (speedGain <= 0.0f) speedGain = 0.0f;
        if (speedGain >= speedMax) speedGain = speedMax;

        if (al::isPadHoldZL(-1) || al::isPadHoldA(-1)) playerPos->y -= (vspeed + speedGain / 6);
        if (al::isPadHoldB(-1)) playerPos->y += (vspeed + speedGain / 6);
    }
}

void controlLol(StageScene *scene) {
    auto actor = rs::getPlayerActor(scene);

    if(Statics::isNoclip)
        graNoclipCode(actor);
    
    if(!Statics::isHUD && scene->mSceneLayout->isWait()) {
        scene->mSceneLayout->end();
        MapMini* compass = scene->mSceneLayout->mMapMiniLyt;
        if (compass->mIsAlive) compass->end();
    }

    if(!Statics::isPlayMusic) {
        if (al::isPlayingBgm(scene)) {
            al::stopAllBgm(scene, 0);
        }
    }
}

static void drawPlayerInfo(al::LiveActor* actor) {
    // Translation
    static float trans[3];
    static bool transLock = false;
    ImGui::Checkbox("Translation Lock", &transLock);
    if(!transLock) {
        sead::Vector3f seadTrans = al::getTrans(actor);
        trans[0] = seadTrans.x;
        trans[1] = seadTrans.y;
        trans[2] = seadTrans.z;
    }
    ImGui::DragFloat3("Translation", trans, 5.f);
    if(transLock)
        al::setTrans(actor, {trans[0], trans[1], trans[2]});

    // Velocity
    static float velocity[3];
    static bool velocityLock = false;
    ImGui::Checkbox("Velocity Lock", &velocityLock);
    if(!velocityLock) {
        sead::Vector3f seadVelocity = al::getVelocity(actor);
        velocity[0] = seadVelocity.x;
        velocity[1] = seadVelocity.y;
        velocity[2] = seadVelocity.z;
    }
    ImGui::DragFloat3("Velocity", velocity, 0.5f);
    if(velocityLock)
        al::setVelocity(actor, {velocity[0], velocity[1], velocity[2]});

    // Gravity
    float gravity[3];
    sead::Vector3f seadGravity = al::getGravity(actor);
    gravity[0] = seadGravity.x;
    gravity[1] = seadGravity.y;
    gravity[2] = seadGravity.z;
    ImGui::DragFloat3("Gravity", gravity);

    // Quaternion
    float quat[4];
    sead::Quatf seadQuat = al::getQuat(actor);
    quat[0] = seadQuat.w;
    quat[1] = seadQuat.x;
    quat[2] = seadQuat.y;
    quat[3] = seadQuat.z;
    ImGui::DragFloat4("Quaternion", quat);

    // Front
    float front[3];
    sead::Vector3f frontAngle;
    al::calcFrontDir(&frontAngle, actor);
    front[0] = frontAngle.x;
    front[1] = frontAngle.y;
    front[2] = frontAngle.z;
    ImGui::DragFloat3("Front", front);
}

static void drawHeapInfo(sead::Heap* heap, const char* heapName) {
    size_t freeSize = heap->getFreeSize();
    size_t maxSize = heap->getSize();
    float freeSizeF = static_cast<float>(freeSize);
    float maxSizeF = static_cast<float>(maxSize);

    float percentage = freeSizeF / maxSizeF * 100.f;

    ImGui::BulletText("%s %u/%u (%.02f%%)", heapName, freeSize, maxSize, percentage);
}

static void drawImGuiBranchInfo(){
    ImGui::BulletText("0 - Not Selected");
    ImGui::BulletText("1 - Forest");
    ImGui::BulletText("2 - Lake");
    ImGui::BulletText("3 - Sea");
    ImGui::BulletText("4 - Snow");
}

void drawDebugWindow() {
    if(!Statics::showWindow) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        return;
    } else if(Statics::isFirstShowFrame) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        Statics::isFirstShowFrame = false;
    }

    al::Sequence *curSequence = GameSystemFunction::getGameSystem()->mCurSequence;

    ImGui::Begin("Mod Debugging Tools");
    ImGui::SetWindowPos(ImVec2(860, 220), ImGuiCond_FirstUseEver);
    ImGui::SetWindowSize(ImVec2(400, 485), ImGuiCond_FirstUseEver);
    ImGui::SetWindowFontScale(1.5f);

    static bool isOverrideScenario = false;
    static int scenarioSlide = 1;

    if (curSequence && al::isEqualString(curSequence->getName().cstr(), "HakoniwaSequence")) {
        auto gameSeq = (HakoniwaSequence *) curSequence;
        auto curScene = gameSeq->curScene;
        bool isInGame = curScene && curScene->mIsAlive && al::isEqualString(curScene->mName.cstr(), "StageScene");

        if(ImGui::CollapsingHeader("Settings")) {
            ImGui::Checkbox("Play Music", &Statics::isPlayMusic);
            ImGui::Checkbox("Autosaving", &Statics::isUseAutosaves);   
            ImGui::Checkbox("Show HUD", &Statics::isHUD);
            ImGui::Checkbox("Noclip Flight", &Statics::isNoclip);
        }

        if (isInGame) {
            StageScene *stageScene = (StageScene *) gameSeq->curScene;
            PlayerActorBase *playerBase = rs::getPlayerActor(stageScene);

            if(!Statics::isNoclip && !rs::isActiveDemo(playerBase))
                al::onCollide(playerBase);

            if(ImGui::CollapsingHeader("Player Info")) {
                ImGui::SetWindowFontScale(1.2f);
                ImGui::BulletText("Is Dead: %s", BTOC(PlayerFunction::isPlayerDeadStatus(playerBase)));
                ImGui::BulletText("Is Active Demo: %s", BTOC(rs::isActiveDemo(playerBase)));
                ImGui::BulletText("Is Grounded: %s", BTOC(rs::isPlayerOnGround(playerBase)));

                if (ImGui::TreeNode("Main Player")) {
                    drawPlayerInfo(playerBase);
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("HackCap")) {
                    bool isYukimaru = !playerBase->getPlayerInfo();
                    if(!isYukimaru)
                        drawPlayerInfo(((PlayerActorHakoniwa*)playerBase)->mHackCap);
                    
                    ImGui::TreePop();
                }
                ImGui::SetWindowFontScale(1.5f);
            }

            if (ImGui::CollapsingHeader("Capture Info")) {
                PlayerActorHakoniwa* playerHak = nullptr;
                bool isYukimaru = !playerBase->getPlayerInfo();
                if(isYukimaru) {
                    ImGui::BulletText("Player is not a PlayerActorHakoniwa!");
                }// else {
                //     playerHak = (PlayerActorHakoniwa*)playerBase;
                //     al::LiveActor* 
                // }
            }
        } // Check is in game

        if(ImGui::CollapsingHeader("Memory Info")) {
            ImGui::SetWindowFontScale(1.2f);

            // Draw FPS Graph
            float curFPS = Application::instance()->mFramework->calcFps();
            
            static float values[250] = {};
            static int values_offset = 0;
            static double refresh_time = 0.0;
            if (refresh_time == 0.0)
                refresh_time = ImGui::GetTime();

            while (refresh_time < ImGui::GetTime()) {
                values[values_offset] = curFPS;
                values_offset = (values_offset + 1) % IM_ARRAYSIZE(values);
                refresh_time += 1.0f / 15.0f;
            }

            char overlay[16];
            sprintf(overlay, "%.00f FPS", curFPS);
            ImGui::PlotLines("FPS", values, IM_ARRAYSIZE(values), values_offset, overlay, 0.0f, 60.0f, ImVec2(0, 40.0f));

            if(isInGame) {
                al::LiveActorGroup* actors = curScene->mLiveActorKit->mLiveActorGroup2;
                ImGui::BulletText("Actor Count %i / %i", actors->mActorCount, actors->mMaxActorCount);

                drawHeapInfo(al::getSceneHeap(), "Scene");
                drawHeapInfo(al::getSequenceHeap(), "Sequence");
                drawHeapInfo(al::getSceneResourceHeap(), "Scene Resource");
                drawHeapInfo(al::getWorldResourceHeap(), "World Resource");
            }

            ImGui::SetWindowFontScale(1.5f);
        }

        if(ImGui::CollapsingHeader("Input Info")) {
            sead::Vector2f* lStick = al::getLeftStick(-1);
            sead::Vector2f* rStick = al::getRightStick(-1);
            float stickArray[2];

            stickArray[0] = lStick->x;
            stickArray[1] = lStick->y;
            ImGui::DragFloat2("Left Stick", stickArray);

            stickArray[0] = rStick->x;
            stickArray[1] = rStick->y;
            ImGui::DragFloat2("Right Stick", stickArray);

            if(al::isPadHoldA(-1))
                ImGui::BulletText("Face - A");
            if(al::isPadHoldB(-1))
                ImGui::BulletText("Face - B");
            if(al::isPadHoldX(-1))
                ImGui::BulletText("Face - X");
            if(al::isPadHoldY(-1))
                ImGui::BulletText("Face - Y");
            if(al::isPadHoldL(-1))
                ImGui::BulletText("Shoulder - L");
            if(al::isPadHoldR(-1))
                ImGui::BulletText("Shoulder - R");
            if(al::isPadHoldZL(-1))
                ImGui::BulletText("Trigger - ZL");
            if(al::isPadHoldZR(-1))
                ImGui::BulletText("Trigger - ZR");
        }

        if(ImGui::CollapsingHeader("Primitive Rendering")) {
            ImGui::Checkbox("Draw Primitives", &Statics::primitiveDraw);
            if(Statics::primitiveDraw) {
                ImGui::Checkbox("Draw Nearby Triangles", &Statics::primitiveDrawIsCollision);
                if(Statics::primitiveDrawIsCollision)
                    ImGui::Checkbox("Use Complex Triangle Finder", &Statics::primitiveDrawIsCollisionComplex);
                ImGui::Checkbox("Draw Death Areas", &Statics::primitiveDrawIsDeath);
                ImGui::Checkbox("Draw Stage Change Areas", &Statics::primitiveDrawIsStage);
                ImGui::Checkbox("Draw Water Areas", &Statics::primitiveDrawIsWater);
                ImGui::Checkbox("Draw 2D Areas", &Statics::primitiveDrawIs2D);
            }
        }

        if (ImGui::CollapsingHeader("PlayerHitPointData Editor")) {
            static bool isOverrideHealth = false;
            static int playerHealth = 3;
            ImGui::Checkbox("Override Health", &isOverrideHealth);

            if(isOverrideHealth) {
                ImGui::SliderInt("Health", &playerHealth, 1, 9);
                gameSeq->mGameDataHolder.mData->mGameDataFile->mPlayerHintPointData->mCurrentHit = playerHealth;
            }
            
            if(isInGame) {
                if (ImGui::Button("Kill Player")) {
                    StageScene *stageScene = (StageScene *) gameSeq->curScene;
                    PlayerActorBase *playerBase = rs::getPlayerActor(stageScene);
                    PlayerHelper::killPlayer(playerBase);
                }
            }
        }

        if(ImGui::CollapsingHeader("Outfit Editor")) {
            ImGui::Checkbox("Override Player Outfit", &Statics::isOverrideOutfit);
            if(Statics::isOverrideOutfit) {
                // Select player body
                if(ImGui::BeginCombo("Body", Statics::outfitOverrideBodyName)) {
                    for(int n = 0; n < IM_ARRAYSIZE(bodyNames); n++) {
                        bool is_selected = (Statics::outfitOverrideBodyName == bodyNames[n]); // You can store your selection however you want, outside or inside your objects
                        if (ImGui::Selectable(bodyNames[n], is_selected))
                            Statics::outfitOverrideBodyName = bodyNames[n];
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
                    }

                    ImGui::EndCombo();
                }

                //Select player cap
                if(ImGui::BeginCombo("Cap", Statics::outfitOverrideCapName)) {
                    for(int n = 0; n < IM_ARRAYSIZE(capNames); n++) {
                        bool is_selected = (Statics::outfitOverrideCapName == capNames[n]); // You can store your selection however you want, outside or inside your objects
                        if (ImGui::Selectable(capNames[n], is_selected))
                            Statics::outfitOverrideCapName = capNames[n];
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
                    }

                    ImGui::EndCombo();
                }
                
                if(isInGame) {
                    if(ImGui::Button("Reload Scene")) {
                        StageScene *stageScene = (StageScene *) gameSeq->curScene;
                        ChangeStageInfo stageInfo(gameSeq->mGameDataHolder.mData, "start",
                            GameDataFunction::getCurrentStageName(stageScene), false, -1, ChangeStageInfo::SubScenarioType::UNK);
                            
                        GameDataFunction::tryChangeNextStage(gameSeq->mGameDataHolder, &stageInfo);
                    }
                }
            }
        }

        if (ImGui::CollapsingHeader("Game Progress Editor")) {
            GameProgressData* progressData = gameSeq->mGameDataHolder.mData->mGameDataFile->mProgressData;
            if(ImGui::TreeNode("Waterfall World Progress")) {
                static int waterfallProgress = 0;
                ImGui::BulletText("0 - Arrive");
                ImGui::BulletText("1 - First Moon Collected");
                ImGui::BulletText("2 - Talked to Cappy");
                
                ImGui::SliderInt("Progress", &waterfallProgress, 0, 2);
                if(ImGui::Button("Set Progress"))
                    progressData->mWaterfallWorldProgress = (WaterfallWorldProgressStates)waterfallProgress;

                ImGui::TreePop();
            }

            if(ImGui::TreeNode("Home Ship Status")) {
                static int homeShipState = 0;
                ImGui::BulletText("0 - Broken");
                ImGui::BulletText("1 - Activate");
                ImGui::BulletText("2 - First Launch");
                ImGui::BulletText("3 - Arrive in Cloud");
                ImGui::BulletText("4 - Crash in Lost");
                ImGui::BulletText("5 - Repair");
                ImGui::BulletText("6 - Crash in Ruined");
                ImGui::BulletText("7 - Final Repair");
                
                ImGui::SliderInt("State", &homeShipState, 0, 7);
                if(ImGui::Button("Set State"))
                    progressData->mHomeStatus = (HomeShipStates)homeShipState;

                ImGui::TreePop();
            }

            if(ImGui::TreeNode("Home Ship Branches")) {
                static int branch1State = 0;
                static int branch2State = 0;
                drawImGuiBranchInfo();
                
                ImGui::SliderInt("Branch 1", &branch1State, 0, 4);
                ImGui::SliderInt("Branch 2", &branch2State, 0, 4);
                if(ImGui::Button("Set Branches")) {
                    progressData->mUnlockStateFirstBranch = (UnlockBranchStates)branch1State;
                    progressData->mUnlockStateSecondBranch = (UnlockBranchStates)branch2State;
                }

                ImGui::TreePop();
            }

            if(ImGui::TreeNode("Home Ship Level")) {
                static int shipLevel = 0;
                ImGui::SliderInt("Ship Level", &shipLevel, 0, 9);
                if(ImGui::Button("Set Level"))
                    progressData->mHomeLevel = shipLevel;

                ImGui::TreePop();
            }

            if(ImGui::TreeNode("Unlocked Worlds")) {
                static int unlockWorldNum = 1;
                ImGui::SliderInt("Worlds", &unlockWorldNum, 1, 17);
                if(ImGui::Button("Set Unlocked Worlds"))
                    progressData->mUnlockWorldNum = unlockWorldNum;
                
                ImGui::TreePop();
            }
        }

        if (ImGui::CollapsingHeader("Miscellaneous Save Data")) {
            GameDataFile* file = gameSeq->mGameDataHolder.mData->mGameDataFile;

            if (ImGui::TreeNode("Cappy State")) {
                static bool isHaveCap = true;

                ImGui::Checkbox("Is Have Cappy?", &isHaveCap);
                if(ImGui::Button("Set Cappy State"))
                    file->mIsEnableCap = isHaveCap;
                ImGui::TreePop();
            }
            
            if (ImGui::TreeNode("Coin Counter")) {
                static bool isOverrideCoins = false;
                static int coinCount = 9999;

                ImGui::DragInt("Coins", &coinCount, 25.f, -9999, 99999);
                ImGui::Checkbox("Override Coins", &isOverrideCoins);
                if(isOverrideCoins) {
                    file->mCoinCount = coinCount;
                    if(isInGame) {
                        StageScene *stageScene = (StageScene *) gameSeq->curScene;
                        stageScene->mSceneLayout->coinCounter->updateCountImmidiate();
                    }
                }

                ImGui::TreePop();
            }
        }

        if (ImGui::CollapsingHeader("World List")) {
            ImGui::Checkbox("Override Scenario", &isOverrideScenario);
            if(isOverrideScenario)
                ImGui::SliderInt("Scenario", &scenarioSlide, 1, 15);
            else
                scenarioSlide = 1;

            for (auto &entry: gameSeq->mGameDataHolder.mData->mWorldList->mWorldList) {
                if (ImGui::TreeNode(entry.mMainStageName)) {

                    if (isInGame) {
                        if (ImGui::Button("Warp to World")) {
                            ChangeStageInfo stageInfo(gameSeq->mGameDataHolder.mData, "start", entry.mMainStageName, false, scenarioSlide, ChangeStageInfo::SubScenarioType::UNK);
                            GameDataFunction::tryChangeNextStage(gameSeq->mGameDataHolder, &stageInfo);
                        }
                    }

                    ImGui::BulletText("Clear Main Scenario: %d", entry.mClearMainScenario);
                    ImGui::BulletText("Ending Scenario: %d", entry.mEndingScenario);
                    ImGui::BulletText("Moon Rock Scenario: %d", entry.mMoonRockScenario);

                    if (ImGui::TreeNode("Main Quest Infos")) {
                        for (int i = 0; i < entry.mQuestInfoCount; ++i) {
                            ImGui::BulletText("Quest %d Scenario: %d", i, entry.mMainQuestIndexes[i]);
                        }
                        ImGui::TreePop();
                    }

                    if (ImGui::CollapsingHeader("Database Entries")) {
                        for (auto &dbEntry: entry.mStageNames) {
                            if (ImGui::TreeNode(dbEntry.mStageName.cstr())) {
                                ImGui::BulletText("Stage Category: %s", dbEntry.mStageCategory.cstr());
                                ImGui::BulletText("Stage Use Scenario: %d", dbEntry.mUseScenario);

                                if (isInGame) {
                                    ImGui::Bullet();
                                    if (ImGui::SmallButton("Warp to Stage")) {
                                        ChangeStageInfo stageInfo(gameSeq->mGameDataHolder.mData, "start", dbEntry.mStageName.cstr(), false, scenarioSlide, ChangeStageInfo::SubScenarioType::UNK);
                                        GameDataFunction::tryChangeNextStage(gameSeq->mGameDataHolder, &stageInfo);
                                    }
                                }

                                ImGui::TreePop();
                            }
                        }
                    }

                    ImGui::TreePop();
                }
            }
        }

        if (ImGui::CollapsingHeader("Custom World List")) {
            uint catCount = Statics::devStageListByaml.getSize();

            for(int catIdx = 0; catIdx < catCount; catIdx++) {
                al::ByamlIter catDict = Statics::devStageListByaml.getIterByIndex(catIdx);
                const char* catName = "null";
                const char* catDesc = "null";

                if(!al::tryGetByamlString(&catName, catDict, "CategoryName"))
                    ImGui::Text("Category does not have name!");

                if(!al::tryGetByamlString(&catDesc, catDict, "CategoryDesc"))
                    ImGui::Text("Category does not have description!");
                
                if (ImGui::TreeNode(catName)) {
                    al::ByamlIter stageDict = catDict.getIterByKey("StageList");
                    uint stageCount = stageDict.getSize();

                    ImGui::Text(catDesc);
                    ImGui::Text("Category Size - %u", stageCount);

                    for(int stageIdx = 0; stageIdx < stageCount; stageIdx++) {
                        const char* stageName;
                        stageDict.tryGetStringByIndex(&stageName, stageIdx);
                        
                        if(ImGui::Button(stageName) && isInGame) {
                            StageScene *stageScene = (StageScene *) gameSeq->curScene;
                            ChangeStageInfo stageInfo(gameSeq->mGameDataHolder.mData, "start",
                                stageName, false, 1, ChangeStageInfo::SubScenarioType::UNK);
                                
                            GameDataFunction::tryChangeNextStage(gameSeq->mGameDataHolder, &stageInfo);
                        }
                    } // Stage name loop

                    ImGui::TreePop(); // Pop current category tree
                } // Exit current category tree
            } // Exit category loop
        }

    } // Check sequence

    ImGui::End();
}

HOOK_DEFINE_TRAMPOLINE(ControlHook) {
    static void Callback(StageScene *scene) {
        controlLol(scene);
        Orig(scene);
    }
};

HOOK_DEFINE_TRAMPOLINE(CreateFileDeviceMgr) {
    static void Callback(sead::FileDeviceMgr *thisPtr) {

        Orig(thisPtr);

        thisPtr->mMountedSd = nn::fs::MountSdCardForDebug("sd");

        sead::NinSDFileDevice *sdFileDevice = new sead::NinSDFileDevice();

        thisPtr->mount(sdFileDevice);
    }
};

HOOK_DEFINE_TRAMPOLINE(RedirectFileDevice) {
    static sead::FileDevice *
    Callback(sead::FileDeviceMgr *thisPtr, sead::SafeString &path, sead::BufferedSafeString *pathNoDrive) {

        sead::FixedSafeString<32> driveName;
        sead::FileDevice *device;

        // Logger::log("Path: %s\n", path.cstr());

        if (!sead::Path::getDriveName(&driveName, path)) {

            device = thisPtr->findDevice("sd");

            if (!(device && device->isExistFile(path))) {

                device = thisPtr->getDefaultFileDevice();

                if (!device) {
                    return nullptr;
                }

            } else {
            }

        } else
            device = thisPtr->findDevice(driveName);

        if (!device)
            return nullptr;

        if (pathNoDrive != nullptr)
            sead::Path::getPathExceptDrive(pathNoDrive, path);

        return device;
    }
};

HOOK_DEFINE_TRAMPOLINE(FileLoaderLoadArc) {
    static sead::ArchiveRes *
    Callback(al::FileLoader *thisPtr, sead::SafeString &path, const char *ext, sead::FileDevice *device) {
        
        sead::FileDevice *sdFileDevice = sead::FileDeviceMgr::instance()->findDevice("sd");

        if (sdFileDevice && sdFileDevice->isExistFile(path))
            device = sdFileDevice;

        return Orig(thisPtr, path, ext, device);
    }
};

HOOK_DEFINE_TRAMPOLINE(FileLoaderIsExistFile) {
    static bool Callback(al::FileLoader *thisPtr, sead::SafeString &path, sead::FileDevice *device) {

        sead::FileDevice *sdFileDevice = sead::FileDeviceMgr::instance()->findDevice("sd");

        if (sdFileDevice && sdFileDevice->isExistFile(path))
            device = sdFileDevice;

        return Orig(thisPtr, path, device);
    }
};

HOOK_DEFINE_TRAMPOLINE(FileLoaderIsExistArchive) {
    static bool Callback(al::FileLoader *thisPtr, sead::SafeString &path, sead::FileDevice *device) {
        sead::FileDevice *sdFileDevice = sead::FileDeviceMgr::instance()->findDevice("sd");

        if (sdFileDevice && sdFileDevice->isExistFile(path))
            device = sdFileDevice;

        return Orig(thisPtr, path, device);
    }
};

HOOK_DEFINE_TRAMPOLINE(SaveHook) {
    static bool Callback(StageScene* scene) {
        if (Statics::isUseAutosaves)
            return Orig(scene);
        else
            return false;
    }
};

HOOK_DEFINE_TRAMPOLINE(GameSystemInit) {
    static void Callback(GameSystem *thisPtr) {
        sead::Heap *curHeap = sead::HeapMgr::instance()->getCurrentHeap();

        sead::DebugFontMgrJis1Nvn::createInstance(curHeap);

        if (al::isExistFile(DBG_SHADER_PATH) && al::isExistFile(DBG_FONT_PATH) && al::isExistFile(DBG_TBL_PATH)) {
            sead::DebugFontMgrJis1Nvn::instance()->initialize(curHeap, DBG_SHADER_PATH, DBG_FONT_PATH, DBG_TBL_PATH,
                                                              0x100000);
        }

        if (!al::isExistArchive(DEV_WORLD_LIST_PATH))
            EXL_ABORT(0x69);

        al::Resource* res;
        res = new (curHeap) al::Resource(DEV_WORLD_LIST_PATH);
        if(res->isExistByml("StageList")) {
            al::ByamlIter iter = al::ByamlIter(res->tryGetByml("StageList"));
            Statics::devStageListByaml = iter;
        }

        sead::TextWriter::setDefaultFont(sead::DebugFontMgrJis1Nvn::instance());

        al::GameDrawInfo *drawInfo = Application::instance()->mDrawInfo;

        agl::DrawContext *context = drawInfo->mDrawContext;
        agl::RenderBuffer *renderBuffer = drawInfo->mFirstRenderBuffer;

        sead::Viewport *viewport = new sead::Viewport(*renderBuffer);

        gTextWriter = new sead::TextWriter(context, viewport);

        gTextWriter->setupGraphics(context);

        gTextWriter->mColor = sead::Color4f(1.f, 1.f, 1.f, 0.8f);

        Orig(thisPtr);

    }
};

static void drawTriangle(sead::PrimitiveRenderer* renderer, al::Triangle* tri)
{
    renderer->drawLine(tri->mPosition1, tri->mPosition2, {0.f, 0.3f, 1.f, 0.7f});
    renderer->drawLine(tri->mPosition2, tri->mPosition3, {0.f, 0.3f, 1.f, 0.7f});
    renderer->drawLine(tri->mPosition3, tri->mPosition1, {0.f, 0.3f, 1.f, 0.7f});
    
    renderer->drawSphere4x8(tri->mPosition1, 6.f, {0.5f, 0.3f, 1.f, 0.6f});
    renderer->drawSphere4x8(tri->mPosition2, 6.f, {0.5f, 0.3f, 1.f, 0.6f});
    renderer->drawSphere4x8(tri->mPosition3, 6.f, {0.5f, 0.3f, 1.f, 0.6f});
}

static void drawAreaGroup(al::Scene* curScene, sead::PrimitiveRenderer* renderer, al::AreaObjGroup* group, sead::Color4f wire = sead::Color4f(0, 255, 0, .1), sead::Color4f solid = sead::Color4f(0, 255, 0, .1))
{
    if(!group)
        return;
    
    sead::Color4f cyl = wire;
    cyl.a *= 0.15f;
    
    for (int i = 0; i < group->mMaxCount; i++) {
        al::AreaObj* area = group->mAreas[i];
        const char* shapeType;
        al::tryGetAreaObjStringArg(&shapeType, area, "ModelName");

        sead::Vector3f scale = area->mAreaShape->mScale;
        sead::Vector3f pos;
        pos.x = area->mAreaMtx.m[0][3];
        pos.y = area->mAreaMtx.m[1][3];
        pos.z = area->mAreaMtx.m[2][3];
        renderer->setModelMatrix(area->mAreaMtx);

        PlayerActorBase* player = rs::getPlayerActor(curScene);

        if (al::isEqualString(shapeType, "AreaCubeBase")) { // origin is at the bottom
            sead::PrimitiveDrawer::CubeArg shapeAreaSolid(sead::Vector3f(0, (scale.y / 2 * 1000), 0), scale * 1000.0f, solid);
            sead::PrimitiveDrawer::CubeArg shapeAreaWire(sead::Vector3f(0, (scale.y / 2 * 1000), 0), scale * 1000.0f, wire);
            renderer->drawCube(shapeAreaSolid);
            renderer->drawWireCube(shapeAreaWire);
        }

        if (al::isEqualString(shapeType, "AreaCubeCenter")) {
            sead::PrimitiveDrawer::CubeArg shapeAreaSolid(sead::Vector3f(0, 0, 0), scale * 1000.0f, solid);
            sead::PrimitiveDrawer::CubeArg shapeAreaWire(sead::Vector3f(0, 0, 0), scale * 1000.0f, wire);
            renderer->drawCube(shapeAreaSolid);
            renderer->drawWireCube(shapeAreaWire);
        }

        if (al::isEqualString(shapeType, "AreaCubeTop")) {
            sead::PrimitiveDrawer::CubeArg shapeAreaSolid(sead::Vector3f(0, -(scale.y / 2 * 1000), 0), scale * 1000.0f, solid);
            sead::PrimitiveDrawer::CubeArg shapeAreaWire(sead::Vector3f(0, -(scale.y / 2 * 1000), 0), scale * 1000.0f, wire);
            renderer->drawCube(shapeAreaSolid);
            renderer->drawWireCube(shapeAreaWire);
        }

        if (al::isEqualString(shapeType, "AreaSphere")) {
            renderer->drawSphere8x16(sead::Vector3f(0, 0, 0), scale.x * 1000, cyl);
        }
        if (al::isEqualString(shapeType, "AreaCylinder")) { // origin is at the bottom
            renderer->drawCylinder32(sead::Vector3f(0, (scale.y / 2 * 1000), 0), scale.x * 1000, scale.y * 1000, cyl);
        }
        if (al::isEqualString(shapeType, "AreaCylinderCenter")) {
            renderer->drawCylinder32(sead::Vector3f(0, 0, 0), scale.x * 1000, scale.y * 1000, cyl);
        }
        if (al::isEqualString(shapeType, "AreaCylinderTop")) {
            renderer->drawCylinder32(sead::Vector3f(0, -(scale.y / 2 * 1000), 0), scale.x * 1000, scale.y * 1000, cyl);
        }
    }
}

HOOK_DEFINE_TRAMPOLINE(DrawDebugMenu) {
    static void Callback(HakoniwaSequence *thisPtr) {
        Orig(thisPtr);

        if(Statics::isOverrideOutfit) {
            GameDataFile* gameFile = thisPtr->mGameDataHolder.mData->mGameDataFile;
            gameFile->mBodyName = Statics::outfitOverrideBodyName;
            gameFile->mCapName = Statics::outfitOverrideCapName;
        }

        if(al::isPadHoldR(-1) && al::isPadHoldZR(-1) && al::isPadTriggerL(-1)) {
            Statics::showWindow = !Statics::showWindow;
            Statics::isFirstShowFrame = true;
        }

        if(!Statics::showWindow)
            return;
        
        agl::DrawContext* drawContext = thisPtr->getDrawInfo()->mDrawContext;

        sead::Vector3f p1 = {-1.f, -0.85f, 0.f}; // top left
        sead::Vector3f p2 = {-0.5f, -0.85f, 0.f}; // top right
        sead::Vector3f p3 = {-1.f, -1.f, 0.f}; // bottom left
        sead::Vector3f p4 = {-0.5f, -1.f, 0.f}; // bottom right
        sead::Color4f c = {0.f, 0.f, 0.f, 0.7f};

        agl::utl::DevTools::beginDrawImm(drawContext, sead::Matrix34<float>::ident, sead::Matrix44<float>::ident);
        agl::utl::DevTools::drawTriangleImm(drawContext, p1, p2, p3, c);
        agl::utl::DevTools::drawTriangleImm(drawContext, p3, p4, p2, c);

        gTextWriter->beginDraw();

        gTextWriter->setCursorFromTopLeft(sead::Vector2f(10.f, 10.f));
        gTextWriter->printf("FPS: %d\n", static_cast<int>(round(Application::instance()->mFramework->calcFps())));

        gTextWriter->setCursorFromTopLeft(sead::Vector2f(10.f, 674.f));
        gTextWriter->setScaleFromFontHeight(19.5f);
        gTextWriter->printf("Sequence: %s\n", thisPtr->getName().cstr());

        al::Scene* curScene = thisPtr->curScene;

        bool isInGame = curScene && curScene->mIsAlive && al::isEqualString(curScene->mName.cstr(), "StageScene");
        if(isInGame && !strstr(typeid(*al::getCurrentNerve(thisPtr)).name(), "Destroy") && Statics::primitiveDraw) {
            gTextWriter->printf("Stage: %s\n", GameDataFunction::getCurrentStageName((StageScene*)curScene));
            gTextWriter->endDraw();

            sead::PrimitiveRenderer* renderer = sead::PrimitiveRenderer::instance();
            const sead::LookAtCamera* cam = al::getLookAtCamera(curScene, 0);
            const sead::Projection* proj = al::getProjectionSead(curScene, 0);

            PlayerActorBase* playerBase = rs::getPlayerActor(curScene);
            PlayerActorHakoniwa* playerHak = nullptr;

            bool isYukimaru = !playerBase->getPlayerInfo();
            if(!isYukimaru)
                playerHak = (PlayerActorHakoniwa*)playerBase;
            
            sead::Vector3f playerPos = al::getTrans(playerBase);

            renderer->setDrawContext(drawContext);
            renderer->setCamera(*cam);
            renderer->setProjection(*proj);
            renderer->setModelMatrix(sead::Matrix34f::ident);

            renderer->begin();

            // Draw Areas
            al::AreaObjGroup* curGroup;
            
            if(Statics::primitiveDrawIsDeath) {
                curGroup = curScene->mLiveActorKit->mAreaObjDirector->getAreaObjGroup("DeathArea");
                drawAreaGroup(curScene, renderer, curGroup, {1.f, 0.f, 0.f, 0.8f}, {1.f, 0.2f, 0.f, 0.01f});
            }

            if(Statics::primitiveDrawIsStage) {
                curGroup = curScene->mLiveActorKit->mAreaObjDirector->getAreaObjGroup("ChangeStageArea");
                drawAreaGroup(curScene, renderer, curGroup, {0.f, 1.f, 0.2f, 0.85f}, {0.f, 0.8f, 0.f, 0.05f});
            }

            if(Statics::primitiveDrawIsWater) {
                curGroup = curScene->mLiveActorKit->mAreaObjDirector->getAreaObjGroup("WaterArea");
                drawAreaGroup(curScene, renderer, curGroup, {0.f, 0.3f, 1.f, 0.85f}, {0.f, 0.f, 0.8f, 0.02f});
            }

            if(Statics::primitiveDrawIs2D) {
                curGroup = curScene->mLiveActorKit->mAreaObjDirector->getAreaObjGroup("2DMoveArea");
                drawAreaGroup(curScene, renderer, curGroup, {1.f, 1.f, 0.f, 0.75f}, {0.6f, 0.6f, 0.f, 0.01f});
            }
            
            //Origin Point
            renderer->setModelMatrix(sead::Matrix34f::ident);
            renderer->drawSphere8x16({0.f ,0.f ,0.f}, 30.f, {1.f, 0.f, 0.f, 0.3f});

            // Player Info
            renderer->drawAxis(playerPos, 150.f);
            renderer->drawAxis({0.f, 0.f, 0.f}, 400000.f);
            
            sead::Vector3f frontTarget;
            al::calcFrontDir(&frontTarget, playerBase);
            frontTarget *= 225.f;
            frontTarget += playerPos;
            renderer->drawLine(playerPos, frontTarget, {1.f, 1.f, 1.f, 1.f});

            if(playerHak) {
                // Draw cappy's current position and velocity direction
                sead::Vector3f capPos = al::getTrans(playerHak->mHackCap);

                renderer->drawSphere4x8(capPos, 12.f, {1.f, 0.3f, 0.3f, 0.4f});
                sead::Vector3f capVelocity = al::getVelocity(playerHak->mHackCap);
                capVelocity *= 20.f;

                capVelocity = capPos + capVelocity;
                renderer->drawLine(capPos, capVelocity, {0.9f, 0.2f, 0.2f, 1.f});
            }

            // Collision Info
            if(Statics::primitiveDrawIsCollision) {
                if(!Statics::primitiveDrawIsCollisionComplex) {
                    sead::Vector3f rayOrg = playerPos;
                    rayOrg.y += 30.f;
                    sead::Vector3f downRay = { 0.f, -500.f, 0.f };
                    sead::Vector3f frontRay;
                    al::calcFrontDir(&frontRay, playerBase);
                    frontRay *= 400.f;

                    al::Triangle downTri;
                    al::Triangle frontTri;

                    bool isDown = alCollisionUtil::getFirstPolyOnArrow(playerBase, nullptr, &downTri, rayOrg, downRay, nullptr, nullptr);
                    bool isFront = alCollisionUtil::getFirstPolyOnArrow(playerBase, nullptr, &frontTri, rayOrg, frontRay, nullptr, nullptr);

                    if(isDown)
                        drawTriangle(renderer, &downTri);
                    if(isFront)
                        drawTriangle(renderer, &frontTri);
                } else {
                    float checkRange = 450.f;
                    float checkInterval = 50.f;
                    sead::Vector3f downRay = { 0.f, -600.f, 0.f };

                    sead::Vector3f frontRay;
                    al::calcFrontDir(&frontRay, playerBase);
                    frontRay *= 600.f;

                    sead::Vector3f rightRay = frontRay;
                    al::rotateVectorDegreeY(&rightRay, 90.f);
                    
                    // Calc downward tris
                    for (float xLoop = -checkRange; xLoop < checkRange; xLoop += checkInterval) {
                        for (float zLoop = -checkRange; zLoop < checkRange; zLoop += checkInterval) {
                            sead::Vector3f origin = playerPos;
                            origin.x += xLoop;
                            origin.y += 250.f;
                            origin.z += zLoop;

                            al::Triangle downTri;
                            bool downFind = alCollisionUtil::getFirstPolyOnArrow(playerBase, nullptr, &downTri, origin, downRay, nullptr, nullptr);
                            if (downFind)
                                drawTriangle(renderer, &downTri);
                        } // Z loop
                    } //X loop

                    // Calc front tris
                    for (float relLoop = -checkRange; relLoop < checkRange; relLoop += checkInterval) {
                        for (float yLoop = -checkRange; yLoop < checkRange; yLoop += checkInterval) {
                            sead::Vector3f origin = playerPos;
                            origin += (rightRay * (relLoop / checkRange));
                            origin.y += yLoop;

                            al::Triangle frontTri;
                            bool frontFind = alCollisionUtil::getFirstPolyOnArrow(playerBase, nullptr, &frontTri, origin, frontRay, nullptr, nullptr);
                            if (frontFind)
                                drawTriangle(renderer, &frontTri);
                        }
                    }
                }
            }

            renderer->end();
        }
    }
};

extern "C" void exl_main(void *x0, void *x1) {
    /* Setup hooking enviroment. */
    envSetOwnProcessHandle(exl::util::proc_handle::Get());
    exl::hook::Initialize();

    runCodePatches();

    GameSystemInit::InstallAtOffset(0x535850);

    // SD File Redirection

    RedirectFileDevice::InstallAtOffset(0x76CFE0);
    FileLoaderLoadArc::InstallAtOffset(0xA5EF64);
    CreateFileDeviceMgr::InstallAtOffset(0x76C8D4);
    FileLoaderIsExistFile::InstallAtOffset(0xA5ED28);
    FileLoaderIsExistArchive::InstallAtOffset(0xA5ED74);

    // Debug Text Writer Drawing
    DrawDebugMenu::InstallAtOffset(0x50F1D8);
    SaveHook::InstallAtSymbol("_ZNK10StageScene12isEnableSaveEv");

    // General Hooks
    ControlHook::InstallAtSymbol("_ZN10StageScene7controlEv");

    // ImGui Hooks
#if IMGUI_ENABLED
    nvnImGui::InstallHooks();

    nvnImGui::addDrawFunc(drawDebugWindow);
#endif

}

extern "C" NORETURN void exl_exception_entry() {
    /* TODO: exception handling */
    EXL_ABORT(0x420);
}