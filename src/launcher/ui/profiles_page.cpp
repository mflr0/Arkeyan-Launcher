// Player profiles in the window (launcher/profiles.h, docs/profiles.md): the active profile's environment and settings, the
// settings file split between the computer and the player, and a game's settings handed back after it ran.
#include "launcher/ui/launcher_app.h"
#include "launcher/profiles.h"
#include "shared/collection/datadir.h"
#include "shared/collection/catalogue.h"
#include "shared/env.h"
#include <fmt/format.h>
#include <SDL3/SDL.h>
#include <atomic>
#include <stb_image.h>
#include "shared/collection/icons.h"
#include <ctime>

namespace launcher_ui
{
    namespace
    {
        // The variables a profile sets (profiles::Environment): kept as the launcher found them, put back for the first profile.
        constexpr const char* ProfileVariables[] = { "SKY_PROFILE_DIR", "SKY_COLLECTION", "SKY_PLAYER_NAME", "SKY_PLAYER_XUID",
            "SKY_PLAYER_PICTURE", "SKY_PLAYER2_NAME", "SKY_PLAYER2_XUID", "SKY_PLAYER2_PICTURE" };
    }

    void App::LoadProfiles()
    {
        for (const char* name : ProfileVariables)
        {
            const char* value = skyenv::Get(name);
            m_profileEnv.emplace_back(name, value != nullptr ? std::optional<std::string>(value) : std::nullopt);
        }
        m_profiles = profiles::Load(Tr("Player 1"));
        UseProfileEnvironment();
        if (!m_profiles.Active().First())
            m_config = ProfileConfig();
    }

    void App::UseProfileEnvironment()
    {
        for (const auto& [name, value] : m_profileEnv)
            if (value)
                SetGameVariable(name.c_str(), value->c_str());
            else
                UnsetGameVariable(name.c_str());
        for (const auto& [name, value] : profiles::Environment(m_profiles.Active()))
            if (value.empty())
                UnsetGameVariable(name.c_str());
            else
                SetGameVariable(name.c_str(), value.c_str());
    }

    LauncherConfig App::ProfileConfig() const
    {
        // The computer's part from launcher.ini, the player's from the profile's file; the first profile's is launcher.ini.
        const LauncherConfig file = LoadLauncherConfig(m_configPath);
        const profiles::Profile& active = m_profiles.Active();
        if (active.First())
            return file;
        LauncherConfig computer, unused;
        profiles::Split(file, computer, unused);
        return profiles::Merge(computer, LoadLauncherConfig(datadir::Text(profiles::SettingsOf(active))));
    }

    bool App::SaveConfig()
    {
        const profiles::Profile& active = m_profiles.Active();
        if (active.First())
            return SaveLauncherConfig(m_configPath, m_config);
        // The computer's keys go to launcher.ini beside the first profile's own keys there; the player's to its file.
        LauncherConfig computer, personal, fileComputer, filePersonal;
        profiles::Split(m_config, computer, personal);
        profiles::Split(LoadLauncherConfig(m_configPath), fileComputer, filePersonal);
        std::error_code error;
        std::filesystem::create_directories(profiles::FolderOf(active), error);
        return SaveLauncherConfig(m_configPath, profiles::Merge(computer, filePersonal)) &&
               SaveLauncherConfig(datadir::Text(profiles::SettingsOf(active)), personal);
    }

    std::string App::GameSettingsPath() const
    {
        // A profile other than the first gives its game a settings file of its own (the computer's keys and its own), handed
        // back when the game ends (TakeBackGameSettings); the first profile's game reads launcher.ini as before profiles.
        const profiles::Profile& active = m_profiles.Active();
        return active.First() ? m_configPath : datadir::Text(profiles::FolderOf(active) / "game.ini");
    }

    void App::TakeBackGameSettings()
    {
        if (m_profiles.Active().First())
            return;
        const std::filesystem::path file = datadir::FromText(GameSettingsPath());
        std::error_code error;
        if (!std::filesystem::exists(file, error))
            return;
        // What the game changed (its F3 menu writes the video settings) comes back into the launcher's settings.
        m_config = LoadLauncherConfig(datadir::Text(file));
        if (!SaveConfig())
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
    }

    void App::SwitchProfile(const std::string& id)
    {
        if (m_profiles.Find(id) == nullptr || id == m_profiles.active || !m_session.Running().empty())
            return;
        if (!SaveConfig())
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
        m_profiles.active = id;
        profiles::Save(m_profiles);
        UseProfileEnvironment();
        m_config = ProfileConfig();
        for (const GameInfo& each : g_games)
        {
            SeedGameVideo(m_config, each.id);
            SeedGameInput(m_config, each.id);
        }
        // Its Toy Box, saves, achievements and play time: read again where they are needed.
        for (const auto& slot : m_slots)
        {
            if (slot->collection)
                slot->collection->ProfileChanged();
            slot->saveBackupsLoaded = false;
            slot->dataSizesLoaded = false;
        }
        RefreshProgress(true);
        // Back where this profile left off: its last game.
        const profiles::Profile& active = m_profiles.Active();
        if (const GameInfo* last = !active.lastGame.empty() ? FindEnabledGame(active.lastGame, m_config) : nullptr)
            SelectGame(*last);
        Toast(Tf("Welcome, {}", active.name), ui::ToastKind::Success);
    }

    void App::RememberProfileGame(const std::string& game)
    {
        profiles::Profile* active = m_profiles.Find(m_profiles.active);
        if (active == nullptr)
            return;
        active->lastGame = game;
        active->lastPlayed = int64_t(std::time(nullptr));
        profiles::Save(m_profiles);
    }

    unsigned App::ProfileColor(const profiles::Profile& profile) const
    {
        return collection::ElementRgb(collection::Element(profile.color));
    }

    void App::DrawAvatar(ImDrawList* draw, ImVec2 center, float radius, const profiles::Profile& profile)
    {
        // The picture in a circle when the profile has one, else its initials on its colour.
        if (profile.hasPicture)
        {
            const std::string path = datadir::Text(profiles::PictureOf(profile));
            auto found = m_avatars.find(path);
            if (found == m_avatars.end())
                found = m_avatars.emplace(path, LoadArt(m_renderer, path, false)).first;
            if (found->second)
            {
                draw->AddImageRounded(ImTextureRef(ImTextureID(found->second.texture)), ImVec2(center.x - radius, center.y - radius),
                    ImVec2(center.x + radius, center.y + radius), found->second.uv0, found->second.uv1, IM_COL32_WHITE, radius);
                return;
            }
        }
        const unsigned color = ProfileColor(profile);
        draw->AddCircleFilled(center, radius, ui::Col(color));
        const std::string initials = profiles::Initials(profile.name);
        const float size = radius * 0.8f;
        ImGui::PushFont(ui::Ctx().fonts.title, size);
        const ImVec2 extent = ImGui::CalcTextSize(initials.c_str());
        draw->AddText(ImVec2(center.x - extent.x / 2, center.y - extent.y / 2), ui::Col(ui::InkOn(color, 0x14141A)), initials.c_str());
        ImGui::PopFont();
    }

    void App::DrawProfileButton()
    {
        // Under the title, as the account in the Minecraft launcher: who plays, and a menu to change.
        const profiles::Profile& active = m_profiles.Active();
        const float avatar = 34;
        ui::ListRowOptions row;
        row.subtitle = Tr("Change profile");
        row.chevron = true;
        row.width = SidebarW - 2 * ui::space::S;
        row.imageWidth = avatar;
        row.leadWidth = avatar + ui::space::S;
        row.image = [&](ImDrawList* draw, ImVec2 at, ImVec2 size) {
            const ImVec2 center(at.x + size.x / 2, at.y + size.y / 2);
            draw->AddCircle(center, avatar / 2 + 2.5f, ui::Col(ProfileColor(active)), 0, 2.0f);
            DrawAvatar(draw, center, avatar / 2, active);
        };
        if (ui::ListRow((active.name + "##profile").c_str(), row))
            ui::OpenMenu("##profiles");
        if (ui::BeginMenu("##profiles"))
        {
            const bool running = !m_session.Running().empty();
            for (const profiles::Profile& profile : m_profiles.profiles)
                if (ui::MenuItem((profile.name + "##" + profile.id).c_str(), { ui::Icon::None, profile.id == m_profiles.active, false, running }))
                    SwitchProfile(profile.id);
            if (running)
                ui::MenuItem(Tr("Quit the game to change profile"), { ui::Icon::None, false, false, true });
            ui::MenuSeparator();
            if (ui::MenuItem(Tr("Add a profile"), { ui::Icon::Plus, false, false, running }))
                OpenProfileEditor({});
            if (ui::MenuItem(Tr("Edit my profile"), { ui::Icon::None }))
                OpenProfileEditor(m_profiles.active);
            if (ui::MenuItem(Tr("Manage the profiles"), { ui::Icon::Gear }))
            {
                GoTo(Page::Settings);
                m_tab = SettingsTab::Profiles;
            }
            ui::EndMenu();
        }
    }

    namespace
    {
        // A picture file the player chose (PNG or JPEG), as a square: its middle, the largest that fits, at 256 px.
        bool SquarePicture(const std::string& from, const std::filesystem::path& to)
        {
            collection::Image image = collection::LoadPng(from);
            if (image.Empty())
            {
                int w = 0, h = 0, channels = 0;
                if (stbi_uc* pixels = stbi_load(from.c_str(), &w, &h, &channels, 4))
                {
                    image.width = w, image.height = h;
                    image.rgba.assign(reinterpret_cast<const uint32_t*>(pixels), reinterpret_cast<const uint32_t*>(pixels) + size_t(w) * size_t(h));
                    stbi_image_free(pixels);
                }
            }
            if (image.Empty())
                return false;
            const int side = std::min(image.width, image.height);
            collection::Image square{ side, side, std::vector<uint32_t>(size_t(side) * size_t(side)) };
            const int x0 = (image.width - side) / 2, y0 = (image.height - side) / 2;
            for (int y = 0; y < side; y++)
                std::copy_n(image.rgba.begin() + size_t(y0 + y) * image.width + x0, side, square.rgba.begin() + size_t(y) * side);
            std::error_code error;
            std::filesystem::create_directories(to.parent_path(), error);
            return collection::SavePng(collection::Resize(square, 256, 256), datadir::Text(to));
        }

        std::string ChoosePicture(SDL_Window* window)
        {
            static const SDL_DialogFileFilter filters[] = { { "Image (PNG, JPEG)", "png;jpg;jpeg" } };
            struct Result { std::atomic<bool> done{ false }; std::string path; };
            Result result;
            SDL_PropertiesID properties = SDL_CreateProperties();
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(filters));
            SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
            SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, [](void* userdata, const char* const* files, int) {
                auto* r = static_cast<Result*>(userdata);
                if (files != nullptr && files[0] != nullptr)
                    r->path = files[0];
                r->done = true;
            }, &result, properties);
            SDL_DestroyProperties(properties);
            while (!result.done)
            {
                SDL_PumpEvents();
                SDL_Delay(10);
            }
            return result.path;
        }
    }

    void App::OpenProfileEditor(const std::string& id)
    {
        ProfileEditor editor;
        editor.open = true;
        editor.id = id;
        if (const profiles::Profile* profile = m_profiles.Find(id))
        {
            snprintf(editor.name, sizeof(editor.name), "%s", profile->name.c_str());
            editor.color = profile->color;
        }
        else
            editor.color = int(m_profiles.profiles.size() * 3 % 10); // a colour of its own, most of the time
        m_profileEditor = editor;
    }

    void App::DrawProfileEditor()
    {
        ProfileEditor& editor = m_profileEditor;
        if (!editor.open)
            return;
        const profiles::Profile* existing = m_profiles.Find(editor.id);
        bool open = true;
        if (!ui::BeginModal("##profileeditor", existing != nullptr ? Tr("Edit the profile") : Tr("New profile"), &open, ui::ModalSize::Medium))
        {
            editor.open = open;
            return;
        }
        // The picture as it will be, at the left; the pseudo, the picture's choices and the colour at the right.
        profiles::Profile preview = existing != nullptr ? *existing : profiles::Profile{};
        preview.name = profiles::CleanName(editor.name);
        preview.color = editor.color;
        if (editor.removePicture)
            preview.hasPicture = false;
        const float big = 120;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 center(at.x + big / 2 + 4, at.y + big / 2 + 4);
        draw->AddCircle(center, big / 2 + 4, ui::Col(ProfileColor(preview)), 0, 3.0f);
        if (!editor.picture.empty())
        {
            auto found = m_avatars.find(editor.picture);
            if (found == m_avatars.end())
                found = m_avatars.emplace(editor.picture, LoadArt(m_renderer, editor.picture, false)).first;
            if (found->second)
                draw->AddImageRounded(ImTextureRef(ImTextureID(found->second.texture)), ImVec2(center.x - big / 2, center.y - big / 2),
                    ImVec2(center.x + big / 2, center.y + big / 2), found->second.uv0, found->second.uv1, IM_COL32_WHITE, big / 2);
        }
        else
            DrawAvatar(draw, center, big / 2, preview);
        ImGui::SetCursorScreenPos(ImVec2(at.x + big + ui::space::L, at.y));
        ImGui::BeginGroup();
        ui::Text(ui::TextStyle::Caption, Tr("Pseudo, shown in the games"), ui::color::InkSoft);
        ui::TextField("##profilename", editor.name, sizeof(editor.name), { .hint = Tr("Player 1"), .width = 300 });
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        ui::Text(ui::TextStyle::Caption, Tr("Picture"), ui::color::InkSoft);
        if (ui::Button(Tr("Import an image..."), { .icon = ui::Icon::Folder }))
            if (std::string chosen = ChoosePicture(m_window); !chosen.empty())
            {
                const std::filesystem::path scratch = profiles::Folder() / ".picture.png";
                if (SquarePicture(chosen, scratch))
                {
                    m_avatars.erase(datadir::Text(scratch));
                    editor.picture = datadir::Text(scratch);
                    editor.removePicture = false;
                }
                else
                    Toast(Tr("Couldn't read this image"), ui::ToastKind::Error);
            }
        if (preview.hasPicture || !editor.picture.empty())
        {
            ImGui::SameLine(0, ui::space::S);
            if (ui::Button(Tr("Initials"), { .kind = ui::ButtonKind::Ghost }))
                editor.picture.clear(), editor.removePicture = true;
        }
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        ui::Text(ui::TextStyle::Caption, Tr("Colour"), ui::color::InkSoft);
        for (int color = 0; color < 10; color++)
        {
            if (color > 0)
                ImGui::SameLine(0, ui::space::XS);
            ImGui::PushID(color);
            const ImVec2 dot = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##colour", ImVec2(24, 24), ImGuiButtonFlags_EnableNav))
                editor.color = color;
            const ImVec2 c(dot.x + 12, dot.y + 12);
            draw->AddCircleFilled(c, 10, ui::Col(collection::ElementRgb(collection::Element(color))));
            if (editor.color == color)
                draw->AddCircle(c, 12.5f, ui::Col(0xFFFFFF), 0, 2.0f);
            ImGui::PopID();
        }
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(at.x, std::max(ImGui::GetCursorScreenPos().y, at.y + big + 8) + ui::space::M));
        const bool named = !preview.name.empty();
        const ui::ModalResult result = ui::ModalActions(&open, named ? (existing != nullptr ? Tr("Save") : Tr("Create the profile")) : nullptr, Tr("Cancel"));
        ui::EndModal();
        if (result == ui::ModalResult::Confirm && named)
        {
            profiles::Profile* profile = existing != nullptr ? m_profiles.Find(editor.id) : &profiles::Add(m_profiles, preview.name, editor.color);
            profile->name = preview.name;
            profile->color = editor.color;
            std::error_code error;
            const std::filesystem::path picture = profiles::PictureOf(*profile);
            if (!editor.picture.empty())
            {
                std::filesystem::create_directories(picture.parent_path(), error);
                std::filesystem::rename(datadir::FromText(editor.picture), picture, error);
                profile->hasPicture = !error;
            }
            else if (editor.removePicture)
            {
                std::filesystem::remove(picture, error);
                profile->hasPicture = false;
            }
            m_avatars.erase(datadir::Text(picture));
            if (!profiles::Save(m_profiles))
                Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            if (existing == nullptr)
                SwitchProfile(profile->id);
            else
                UseProfileEnvironment(); // its name or picture for the next game
        }
        editor.open = open && result == ui::ModalResult::None;
    }

    void App::DrawProfilesTab()
    {
        // Every profile: its picture, its pseudo, its last game; Edit, and Remove (never the last one, never while a game runs).
        const bool running = !m_session.Running().empty();
        ui::SectionHeader(Tr("Profiles"), Tr("Each has its own Toy Box, saves, keys and achievements."));
        if (ui::BeginSettings("##profiles"))
        {
            for (const profiles::Profile& profile : m_profiles.profiles)
            {
                ImGui::PushID(profile.id.c_str());
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float avatar = 40;
                ImGui::Dummy(ImVec2(avatar, avatar));
                DrawAvatar(ImGui::GetWindowDrawList(), ImVec2(at.x + avatar / 2, at.y + avatar / 2), avatar / 2, profile);
                ImGui::SameLine(0, ui::space::M);
                ImGui::BeginGroup();
                const bool active = profile.id == m_profiles.active;
                ui::Text(ui::TextStyle::Body, (active ? Tf("{} (active)", profile.name) : profile.name).c_str(), ui::color::Ink, 0, true);
                const GameInfo* last = profile.lastGame.empty() ? nullptr : FindGame(profile.lastGame);
                ui::Text(ui::TextStyle::Caption, last != nullptr ? Tf("Last played: {}", GameName(last->id, GameNameForm::Short)).c_str() : Tr("Not played yet"),
                    ui::color::InkSoft);
                ImGui::EndGroup();
                ImGui::SameLine();
                const float buttons = ui::ButtonWidth(Tr("Edit")) + ui::space::S + ui::ButtonWidth(Tr("Remove"));
                ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - buttons);
                if (ui::Button(Tr("Edit")))
                    OpenProfileEditor(profile.id);
                ImGui::SameLine(0, ui::space::S);
                // The first profile is the data from before profiles, where it always was: it stays.
                ImGui::BeginDisabled(profile.First() || running);
                if (ui::Button(Tr("Remove"), { .kind = ui::ButtonKind::Ghost }))
                    m_profileToRemove = profile.id;
                ImGui::EndDisabled();
                ImGui::PopID();
                ImGui::Dummy(ImVec2(0, ui::space::XS));
            }
            if (ui::Button(Tr("Add a profile"), { .icon = ui::Icon::Plus }))
                OpenProfileEditor({});
            ui::EndSettings();
        }
        if (const profiles::Profile* gone = m_profiles.Find(m_profileToRemove))
        {
            bool open = true;
            const std::string body = Tf("{}'s Toy Box, saves and keys go to the profiles' trash.", gone->name);
            const ui::ModalResult result = ui::ConfirmDialog("##removeprofile", Tf("Remove {}?", gone->name).c_str(), body.c_str(),
                Tr("Remove"), Tr("Cancel"), &open, ui::ModalKind::Danger);
            if (result == ui::ModalResult::Confirm)
            {
                const std::string id = m_profileToRemove;
                const bool wasActive = id == m_profiles.active;
                if (wasActive)
                    SwitchProfile(m_profiles.profiles.front().id == id ? m_profiles.profiles[1].id : m_profiles.profiles.front().id);
                if (!profiles::Remove(m_profiles, id, profiles::Folder() / ".trash") || !profiles::Save(m_profiles))
                    Toast(Tr("Couldn't remove the profile"), ui::ToastKind::Error);
            }
            if (!open || result != ui::ModalResult::None)
                m_profileToRemove.clear();
        }
    }
}
