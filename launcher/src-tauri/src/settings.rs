//! Settings in `OpenForts.ini` in the game folder. The launcher writes the file and the runtime DLL
//! reads it at start-up with `GetPrivateProfileInt`, so the format stays a plain INI file.

use std::fs;
use std::io::ErrorKind;
use std::path::Path;

use serde::{Deserialize, Serialize};

const FILE_NAME: &str = "OpenForts.ini";

#[derive(Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Settings {
    pub debug: bool,
}

/// Reads the settings; a missing file means defaults.
pub fn load(folder: &Path) -> Result<Settings, String> {
    match fs::read_to_string(folder.join(FILE_NAME)) {
        Ok(text) => Ok(parse(&text)),
        Err(error) if error.kind() == ErrorKind::NotFound => Ok(Settings::default()),
        Err(error) => Err(format!("Could not read {FILE_NAME}: {error}")),
    }
}

pub fn save(folder: &Path, settings: &Settings) -> Result<(), String> {
    let text = format!("[Debug]\r\nEnabled={}\r\n", u8::from(settings.debug));
    fs::write(folder.join(FILE_NAME), text)
        .map_err(|error| format!("Could not write {FILE_NAME}: {error}"))
}

/// Section and key names are case-insensitive, like Windows' own INI functions.
fn parse(text: &str) -> Settings {
    let mut settings = Settings::default();
    let mut section = String::new();
    for line in text.lines().map(str::trim) {
        if let Some(name) = line
            .strip_prefix('[')
            .and_then(|rest| rest.strip_suffix(']'))
        {
            section = name.trim().to_ascii_lowercase();
        } else if let Some((key, value)) = line.split_once('=')
            && section == "debug"
            && key.trim().eq_ignore_ascii_case("enabled")
        {
            settings.debug = value.trim().parse::<i32>().is_ok_and(|number| number != 0);
        }
    }
    settings
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_what_it_writes() {
        let folder = std::env::temp_dir().join("openforts-settings-test");
        fs::create_dir_all(&folder).unwrap();
        save(&folder, &Settings { debug: true }).unwrap();
        assert!(load(&folder).unwrap().debug);
        save(&folder, &Settings { debug: false }).unwrap();
        assert!(!load(&folder).unwrap().debug);
        fs::remove_dir_all(&folder).unwrap();
    }

    #[test]
    fn ignores_case_and_other_sections() {
        assert!(parse("[other]\nEnabled=0\n[DEBUG]\n enabled = 1 \n").debug);
        assert!(!parse("[Other]\nEnabled=1\n").debug);
    }
}
