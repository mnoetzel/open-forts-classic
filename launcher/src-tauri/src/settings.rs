//! Settings in `OpenForts.ini` in the game folder. The launcher writes the file and the runtime DLL
//! reads it at start-up with `GetPrivateProfileInt`/`GetPrivateProfileString`, so the format stays a
//! plain INI file.

use std::fs;
use std::io::ErrorKind;
use std::path::Path;

use serde::{Deserialize, Serialize};

const FILE_NAME: &str = "OpenForts.ini";

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum ControlsProfile {
    #[default]
    Modern,
    Classic,
}

impl ControlsProfile {
    const fn ini_value(self) -> &'static str {
        match self {
            Self::Modern => "Modern",
            Self::Classic => "Classic",
        }
    }
}

#[derive(Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Settings {
    pub debug: bool,
    pub controls: ControlsProfile,
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
    let text = format!(
        "[Debug]\r\nEnabled={}\r\n\r\n[Controls]\r\nProfile={}\r\n",
        u8::from(settings.debug),
        settings.controls.ini_value()
    );
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
        } else if let Some((key, value)) = line.split_once('=') {
            let (key, value) = (key.trim(), value.trim());
            match section.as_str() {
                "debug" if key.eq_ignore_ascii_case("enabled") => {
                    settings.debug = value.parse::<i32>().is_ok_and(|number| number != 0);
                }
                "controls" if key.eq_ignore_ascii_case("profile") => {
                    settings.controls = if value.eq_ignore_ascii_case("classic") {
                        ControlsProfile::Classic
                    } else {
                        ControlsProfile::Modern
                    };
                }
                _ => {}
            }
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
        for (debug, controls) in [
            (true, ControlsProfile::Classic),
            (false, ControlsProfile::Modern),
        ] {
            save(&folder, &Settings { debug, controls }).unwrap();
            let loaded = load(&folder).unwrap();
            assert_eq!(loaded.debug, debug);
            assert_eq!(loaded.controls, controls);
        }
        fs::remove_dir_all(&folder).unwrap();
    }

    #[test]
    fn ignores_case_and_other_sections() {
        let settings =
            parse("[other]\nEnabled=0\n[DEBUG]\n enabled = 1 \n[controls]\nPROFILE = classic\n");
        assert!(settings.debug);
        assert_eq!(settings.controls, ControlsProfile::Classic);
        assert!(!parse("[Other]\nEnabled=1\n").debug);
    }

    #[test]
    fn defaults_to_modern_controls() {
        assert_eq!(parse("").controls, ControlsProfile::Modern);
        assert_eq!(
            parse("[Controls]\nProfile=unknown\n").controls,
            ControlsProfile::Modern
        );
    }
}
