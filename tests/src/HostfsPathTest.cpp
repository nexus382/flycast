#include "gtest/gtest.h"

#include <filesystem>
#include <string>

#include "cfg/option.h"
#include "oslib/oslib.h"
#include "stdclass.h"
#include "types.h"

class HostfsPathTest : public ::testing::Test
{
protected:
    std::filesystem::path tempDir;

    void SetUp() override
    {
        tempDir = std::filesystem::temp_directory_path() / "flycast_hostfs_path_test";
        std::filesystem::remove_all(tempDir);
        std::filesystem::create_directories(tempDir);
        set_user_config_dir(tempDir.string());
        set_user_data_dir(tempDir.string());
        config::SavePath.set("");
        config::VMUPath.set("");
        config::PerGameVmu.set(false);
        settings.content.path.clear();
        settings.content.gameId.clear();
        settings.content.fileName.clear();
        settings.platform.system = DC_PLATFORM_DREAMCAST;
    }

    void TearDown() override
    {
        std::filesystem::remove_all(tempDir);
    }
};

TEST_F(HostfsPathTest, UsesCustomSavePathWhenProvided)
{
    config::SavePath.set((tempDir / "custom_saves").string());
    settings.content.fileName = "arcade.bin";

    const std::string arcadePath = hostfs::getArcadeFlashPath();

    EXPECT_EQ(std::filesystem::path(arcadePath), tempDir / "custom_saves" / "arcade.bin");
}

TEST_F(HostfsPathTest, UsesWritableDataPathWhenSavePathIsEmpty)
{
    config::SavePath.set("");
    settings.content.fileName = "defaultgame.zip";

    const std::string arcadePath = hostfs::getArcadeFlashPath();

    EXPECT_EQ(std::filesystem::path(arcadePath), tempDir / "defaultgame.zip");
}

TEST_F(HostfsPathTest, GeneratesPerGameVmuNameFromGameId)
{
    config::PerGameVmu.set(true);
    settings.content.path = "dummy.cdi";
    settings.content.gameId = "Crazy Taxi:1";

    const std::string vmuPath = hostfs::getVmuPath("A1", true);

    EXPECT_EQ(std::filesystem::path(vmuPath), tempDir / "Crazy_Taxi_1_vmu_save_A1.bin");
}

TEST_F(HostfsPathTest, MatchesSaveArtifactsForSpecificGame)
{
    config::PerGameVmu.set(true);
    settings.content.fileName = "Skies of Arcadia (USA).gdi";
    settings.content.path = (tempDir / "Skies of Arcadia (USA).gdi").string();
    settings.content.gameId = "Skies of Arcadia (USA) [HDR]";

    const std::string flashPath = hostfs::getArcadeFlashPath();
    const std::string savestatePath = hostfs::getSavestatePath(2, true);
    const std::string vmuPath = hostfs::getVmuPath("A1", true);

    EXPECT_EQ(std::filesystem::path(flashPath), tempDir / "Skies of Arcadia (USA).gdi");
    EXPECT_EQ(std::filesystem::path(savestatePath), tempDir / "Skies of Arcadia (USA)_2.state");
    EXPECT_EQ(std::filesystem::path(vmuPath), tempDir / "Skies_of_Arcadia_(USA)_[HDR]_vmu_save_A1.bin");
}
