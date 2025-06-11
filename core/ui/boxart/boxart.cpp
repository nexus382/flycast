/*
	Copyright 2022 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "boxart.h"
#include "gamesdb.h"
#include "../game_scanner.h"
#include "oslib/oslib.h"
#include "oslib/storage.h"
#include "cfg/option.h"
#include <chrono>
#include <filesystem>
#include <set>

GameBoxart Boxart::getBoxart(const GameMedia& media)
{
	loadDatabase();
	GameBoxart boxart;
	{
		std::lock_guard<std::mutex> guard(mutex);
		auto it = games.find(media.fileName);
		if (it != games.end())
			boxart = it->second;
	}
	return boxart;
}

bool Boxart::checkCustomBoxart(GameBoxart& boxart)
{
	std::string baseName = get_file_basename(boxart.fileName);

	// Check for common image formats
	const char* extensions[] = { ".png", ".jpg", ".jpeg", ".webp" };

	// First check in the custom boxart directory
	const std::string customDir = getCustomBoxartPath();

	if (!file_exists(customDir))
		make_directory(customDir);

	for (const char* ext : extensions)
	{
		// Make sure we use the correct path separator for the OS
		const std::string customPath = join_paths(customDir, baseName + ext);

		if (file_exists(customPath))
		{
			boxart.setBoxartPath(customPath);
			boxart.parsed = true;
			return true;
		}
	}

	// Check in user-selected content directories (from General Settings)
	for (const std::string& contentPath : config::ContentPath.get())
	{
#ifdef __ANDROID__
		if (contentPath.substr(0, 10) == "content://")
		{
			// Android content URI - check cache only (populated at startup)
			for (const char* ext : extensions)
			{
				std::string localFile = getSaveDirectory() + "custom_" + baseName + ext;
				if (file_exists(localFile))
				{
					boxart.setBoxartPath(localFile);
					boxart.parsed = true;
					return true;
				}
			}
			continue;
		}
#endif

		// Regular filesystem path - instant changes
		for (const char* ext : extensions)
		{
			const std::string customBoxartDir = join_paths(contentPath, CUSTOM_BOXART_DIRECTORY);

			if (!file_exists(customBoxartDir))
				make_directory(customBoxartDir);

			const std::string fullPath = join_paths(customBoxartDir, baseName + ext);

			if (file_exists(fullPath))
			{
				boxart.setBoxartPath(fullPath);
				boxart.parsed = true;
				return true;
			}
		}
	}

	return false;
}

GameBoxart Boxart::getBoxartAndLoad(const GameMedia& media)
{
	loadDatabase();
	GameBoxart boxart;
	{
		std::lock_guard<std::mutex> guard(mutex);
		auto it = games.find(media.fileName);
		if (it != games.end())
		{
			boxart = it->second;

			// Check for custom boxart first
			if (checkCustomBoxart(boxart))
			{
				games[media.fileName] = boxart;
				databaseDirty = true;
				return boxart;
			}

			if (config::FetchBoxart && !boxart.busy && !boxart.scraped)
			{
				boxart.busy = it->second.busy = true;
				boxart.gamePath = media.path;
				toFetch.push_back(boxart);
			}
		}
		else
		{
			boxart.fileName = media.fileName;
			boxart.gamePath = media.path;
			boxart.name = media.name;
			boxart.searchName = media.gameName;	// for arcade games

			// Check for custom boxart
			if (checkCustomBoxart(boxart))
			{
				games[boxart.fileName] = boxart;
				databaseDirty = true;
				return boxart;
			}

			boxart.busy = true;
			games[boxart.fileName] = boxart;
			toFetch.push_back(boxart);
		}
	}
	fetchBoxart();
	return boxart;
}

void Boxart::fetchBoxart()
{
	if (fetching.valid() && fetching.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		fetching.get();
	if (fetching.valid())
		return;
	if (toFetch.empty())
		return;
	fetching = std::async(std::launch::async, [this]() {
		ThreadName _("BoxArt-scraper");
		if (offlineScraper == nullptr)
		{
			offlineScraper = std::unique_ptr<Scraper>(new OfflineScraper());
			offlineScraper->initialize(getSaveDirectory());
		}
		if (config::FetchBoxart && scraper == nullptr)
		{
			scraper = std::unique_ptr<Scraper>(new TheGamesDb());
			if (!scraper->initialize(getSaveDirectory()))
			{
				ERROR_LOG(COMMON, "thegamesdb scraper initialization failed");
				scraper.reset();
				return;
			}
		}
		std::vector<GameBoxart> boxart;
		{
			std::lock_guard<std::mutex> guard(mutex);
			size_t size = std::min(toFetch.size(), (size_t)10);
			boxart = std::vector<GameBoxart>(toFetch.begin(), toFetch.begin() + size);
			toFetch.erase(toFetch.begin(), toFetch.begin() + size);
		}
		DEBUG_LOG(COMMON, "Scraping %d games", (int)boxart.size());
		offlineScraper->scrape(boxart);
		{
			std::lock_guard<std::mutex> guard(mutex);
			for (GameBoxart& b : boxart)
				if (b.scraped || b.parsed)
				{
					if (!config::FetchBoxart || b.scraped)
						b.busy = false;
					games[b.fileName] = b;
					databaseDirty = true;
				}
		}
		if (config::FetchBoxart)
		{
			try {
				scraper->scrape(boxart);
				{
					std::lock_guard<std::mutex> guard(mutex);
					for (GameBoxart& b : boxart)
					{
						b.busy = false;
						games[b.fileName] = b;
					}
				}
				databaseDirty = true;
			} catch (const std::runtime_error& e) {
				if (*e.what() != '\0')
					INFO_LOG(COMMON, "thegamesdb error: %s", e.what());
				{
					// put back failed items into toFetch array
					std::lock_guard<std::mutex> guard(mutex);
					for (GameBoxart& b : boxart)
						if (b.scraped)
						{
							b.busy = false;
							games[b.fileName] = b;
							databaseDirty = true;
						}
						else
						{
							toFetch.push_back(b);
						}
				}
			}
		}
		saveDatabase();
	});
}

void Boxart::saveDatabase()
{
	if (!databaseDirty)
		return;
	std::string db_name = getSaveDirectory() + DB_NAME;
	FILE *file = nowide::fopen(db_name.c_str(), "wt");
	if (file == nullptr)
	{
		WARN_LOG(COMMON, "Can't save boxart database to %s: error %d", db_name.c_str(), errno);
		return;
	}
	DEBUG_LOG(COMMON, "Saving boxart database to %s", db_name.c_str());

	json array;
	{
		std::lock_guard<std::mutex> guard(mutex);
		for (const auto& game : games)
			if (game.second.scraped || game.second.parsed)
				array.push_back(game.second.to_json());
	}
	std::string serialized = array.dump(4);
	fwrite(serialized.c_str(), 1, serialized.size(), file);
	fclose(file);
	databaseDirty = false;
}

void Boxart::loadDatabase()
{
	if (databaseLoaded)
		return;
	databaseLoaded = true;
	databaseDirty = false;
	std::string save_dir = getSaveDirectory();
	if (!file_exists(save_dir))
		make_directory(save_dir);
	std::string db_name = save_dir + DB_NAME;
	FILE *f = nowide::fopen(db_name.c_str(), "rt");
	if (f == nullptr)
		return;

	DEBUG_LOG(COMMON, "Loading boxart database from %s", db_name.c_str());
	std::string all_data;
	char buf[4096];
	while (true)
	{
		int s = fread(buf, 1, sizeof(buf), f);
		if (s <= 0)
			break;
		all_data.append(buf, s);
	}
	fclose(f);
	try {
		std::lock_guard<std::mutex> guard(mutex);

		json v = json::parse(all_data);
		for (const auto& o : v)
		{
			GameBoxart game(o);
			games[game.fileName] = game;
		}
	} catch (const json::exception& e) {
		WARN_LOG(COMMON, "Corrupted database file: %s", e.what());
	}

	// Create custom boxart directory if it doesn't exist
	std::string customDir = getCustomBoxartPath();
	if (!file_exists(customDir))
		make_directory(customDir);

	// Scan content directories once at startup (Android only)
	scanContentDirectories();
	
	// Check database entries and reset custom flags for missing files
	validateCustomBoxartFlags();
}

void Boxart::term()
{
	if (fetching.valid())
		fetching.get();
}

void Boxart::scanContentDirectories()
{
#ifdef __ANDROID__
	// Keep track of valid custom boxart files that should be cached
	std::set<std::string> validCachedFiles;
	
	// One-time scan at startup to cache custom boxart files from content directories
	for (const auto& contentPath : config::ContentPath.get())
	{
		if (contentPath.substr(0, 10) == "content://")
		{
			try {
				std::string customBoxartDir = hostfs::storage().getSubPath(contentPath, CUSTOM_BOXART_DIRECTORY);
				auto files = hostfs::storage().listContent(customBoxartDir);

				for (const auto& file : files)
				{
					if (!file.isDirectory)
					{
						std::string ext = get_file_extension(file.name);
						if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp")
						{
							std::string baseName = get_file_basename(file.name);
							std::string localFile = getSaveDirectory() + "custom_" + baseName + "." + ext;
							validCachedFiles.insert(localFile);

							// Only copy if we don't already have it cached
							if (!file_exists(localFile))
							{
								FILE* src = hostfs::storage().openFile(file.path, "rb");
								if (src)
								{
									FILE* dst = nowide::fopen(localFile.c_str(), "wb");
									if (dst)
									{
										char buffer[32768];
										size_t bytes;
										while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0)
										{
											if (fwrite(buffer, 1, bytes, dst) != bytes)
												break;
										}
										fclose(dst);
									}
									fclose(src);
								}
							}
						}
					}
				}
			} catch (const FlycastException&) {
				// Continue to next content directory
			}
		}
	}
	
	// Clean up orphaned cached files (files that no longer exist in source directories)
	try {
		std::string saveDir = getSaveDirectory();
		for (const auto& entry : std::filesystem::directory_iterator(saveDir))
		{
			if (entry.is_regular_file())
			{
				std::string filename = entry.path().filename().string();
				if (filename.substr(0, 7) == "custom_")
				{
					std::string fullPath = entry.path().string();
					if (validCachedFiles.find(fullPath) == validCachedFiles.end())
					{
						nowide::remove(fullPath.c_str());
						DEBUG_LOG(COMMON, "Removed orphaned cached custom boxart: %s", filename.c_str());
					}
				}
			}
		}
	} catch (const std::exception& e) {
		WARN_LOG(COMMON, "Error cleaning up cached custom boxart: %s", e.what());
	}
#endif
}

void Boxart::validateCustomBoxartFlags()
{
	std::lock_guard<std::mutex> guard(mutex);
	
	for (auto& game : games)
	{
		// Only check entries marked as having custom boxart
		if (!game.second.parsed)
			continue;
		
		// Check if the custom boxart file actually exists
		bool customExists = false;
		if (!game.second.boxartPath.empty())
		{
			customExists = file_exists(game.second.boxartPath);
		}
		
		if (!customExists)
		{
			// Custom boxart file is missing, revert to scraped image
			game.second.parsed = false;
			game.second.boxartPath.clear();  // Clear the invalid path
			databaseDirty = true;
			DEBUG_LOG(COMMON, "Reset custom boxart flag for %s - custom file missing", game.second.fileName.c_str());
		}
	}
	
	if (databaseDirty)
		saveDatabase();
}
