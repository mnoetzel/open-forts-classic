mod game;

#[tauri::command]
fn game_status() -> Result<game::GameStatus, String> {
    game::status(&game::game_folder()?)
}

#[tauri::command]
fn launch() -> Result<(), String> {
    game::launch(&game::game_folder()?)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![game_status, launch])
        .run(tauri::generate_context!())
        .expect("error while running the launcher");
}
