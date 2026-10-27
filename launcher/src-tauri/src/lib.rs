mod game;
mod settings;

use settings::Settings;

#[tauri::command]
fn game_status() -> Result<game::GameStatus, String> {
    game::status(&game::game_folder()?)
}

#[tauri::command]
fn launch() -> Result<(), String> {
    game::launch(&game::game_folder()?)
}

#[tauri::command]
fn load_settings() -> Result<Settings, String> {
    settings::load(&game::game_folder()?)
}

#[tauri::command]
fn save_settings(settings: Settings) -> Result<(), String> {
    settings::save(&game::game_folder()?, &settings)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            game_status,
            launch,
            load_settings,
            save_settings
        ])
        .run(tauri::generate_context!())
        .expect("error while running the launcher");
}
