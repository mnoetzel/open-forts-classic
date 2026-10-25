//! Detects a supported Worms Forts installation and creates the patched executable.
//!
//! The patched copy differs from `WF.exe` in one place: its import table names our runtime
//! (`OFCRT.DLL`) instead of GOG's `PATCH.DLL`. Both names have nine characters, so the string is
//! replaced in place and nothing else in the file moves.
//!
//! The launcher is a single portable file, so the runtime DLL is embedded in it and written into
//! the game folder on launch.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::{env, fs};

use serde::Serialize;
use sha2::{Digest, Sha256};

pub const ORIGINAL_EXE: &str = "WF.exe";
pub const PATCHED_EXE: &str = "WF-OpenForts.exe";
pub const RUNTIME_DLL: &str = "OFCRT.dll";

/// Build the runtime first (see the README): `cmake --build runtime/build --config Release`.
const RUNTIME_DLL_BYTES: &[u8] = include_bytes!("../../../runtime/build/Release/OFCRT.dll");

/// SHA-256 of the GOG release of `WF.exe`, the only supported build for now.
const GOG_WF_EXE_SHA256: &str = "0221d68e649726338f8a59c51d204f4992e3b8cdfae4b8be518bbd36827501d3";
const GOG_IMPORT: &[u8] = b"PATCH.DLL";
const RUNTIME_IMPORT: &[u8] = b"OFCRT.DLL";

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct GameStatus {
    pub exe_found: bool,
    pub supported: bool,
    pub sha256: Option<String>,
}

/// The game folder is the folder the launcher runs from. Debug builds may point elsewhere with
/// `OPENFORTS_GAME_DIR`, because `pnpm tauri dev` runs the launcher from `target/`.
pub fn game_folder() -> Result<PathBuf, String> {
    if cfg!(debug_assertions)
        && let Some(folder) = env::var_os("OPENFORTS_GAME_DIR")
    {
        return Ok(folder.into());
    }
    let exe =
        env::current_exe().map_err(|error| format!("Could not locate the launcher: {error}"))?;
    exe.parent()
        .map(Path::to_path_buf)
        .ok_or_else(|| "Could not locate the launcher's folder".into())
}

pub fn status(folder: &Path) -> Result<GameStatus, String> {
    let exe = folder.join(ORIGINAL_EXE);
    let sha256 = if exe.is_file() {
        Some(sha256_hex(&read(&exe)?))
    } else {
        None
    };
    Ok(GameStatus {
        exe_found: sha256.is_some(),
        supported: sha256.as_deref() == Some(GOG_WF_EXE_SHA256),
        sha256,
    })
}

/// Writes the patched executable and our runtime into `folder`, then starts the game.
pub fn launch(folder: &Path) -> Result<(), String> {
    let original = read(&folder.join(ORIGINAL_EXE))?;
    if sha256_hex(&original) != GOG_WF_EXE_SHA256 {
        return Err("This WF.exe is not the supported GOG version.".into());
    }
    let patched_path = folder.join(PATCHED_EXE);
    fs::write(&patched_path, patch_import_name(original)?)
        .map_err(|error| format!("Could not write {PATCHED_EXE}: {error}"))?;
    fs::write(folder.join(RUNTIME_DLL), RUNTIME_DLL_BYTES)
        .map_err(|error| format!("Could not write {RUNTIME_DLL}: {error}"))?;

    Command::new(&patched_path)
        .current_dir(folder)
        .spawn()
        .map_err(|error| format!("Could not start the game: {error}"))?;
    Ok(())
}

fn read(path: &Path) -> Result<Vec<u8>, String> {
    fs::read(path).map_err(|error| format!("Could not read {}: {error}", path.display()))
}

fn sha256_hex(data: &[u8]) -> String {
    Sha256::digest(data)
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect()
}

/// Replaces the import of `PATCH.DLL` with `OFCRT.DLL` in a 32-bit PE file.
fn patch_import_name(mut image: Vec<u8>) -> Result<Vec<u8>, String> {
    let pe = Pe::parse(&image)?;
    let mut descriptor = pe.rva_to_offset(pe.import_directory_rva)?;
    let mut matches = Vec::new();
    loop {
        let name_rva = u32_at(&image, descriptor + 12)?;
        if name_rva == 0 {
            break;
        }
        let name_offset = pe.rva_to_offset(name_rva)?;
        let name = image
            .get(name_offset..name_offset + GOG_IMPORT.len() + 1)
            .ok_or("Import name out of range")?;
        if name[..GOG_IMPORT.len()].eq_ignore_ascii_case(GOG_IMPORT) && name[GOG_IMPORT.len()] == 0
        {
            matches.push(name_offset);
        }
        descriptor += 20;
    }
    let [offset] = matches[..] else {
        return Err(format!(
            "Expected one import of PATCH.DLL, found {}",
            matches.len()
        ));
    };
    image[offset..offset + RUNTIME_IMPORT.len()].copy_from_slice(RUNTIME_IMPORT);
    Ok(image)
}

struct Section {
    virtual_address: u32,
    virtual_size: u32,
    raw_offset: u32,
    raw_size: u32,
}

struct Pe {
    import_directory_rva: u32,
    sections: Vec<Section>,
}

impl Pe {
    fn parse(image: &[u8]) -> Result<Self, String> {
        let header = u32_at(image, 0x3c)? as usize;
        if image.get(header..header + 4) != Some(b"PE\0\0".as_slice()) {
            return Err("Not a PE file".into());
        }
        let coff = header + 4;
        let section_count = u16_at(image, coff + 2)? as usize;
        let optional_size = u16_at(image, coff + 16)? as usize;
        let optional = coff + 20;
        if u16_at(image, optional)? != 0x10b {
            return Err("Not a 32-bit PE file".into());
        }
        // PE32: the data directories start 96 bytes into the optional header; imports are entry 1.
        let import_directory_rva = u32_at(image, optional + 96 + 8)?;
        let table = optional + optional_size;
        let sections = (0..section_count)
            .map(|index| {
                let entry = table + index * 40;
                Ok(Section {
                    virtual_size: u32_at(image, entry + 8)?,
                    virtual_address: u32_at(image, entry + 12)?,
                    raw_size: u32_at(image, entry + 16)?,
                    raw_offset: u32_at(image, entry + 20)?,
                })
            })
            .collect::<Result<_, String>>()?;
        Ok(Self {
            import_directory_rva,
            sections,
        })
    }

    fn rva_to_offset(&self, rva: u32) -> Result<usize, String> {
        self.sections
            .iter()
            .find(|section| {
                rva >= section.virtual_address
                    && rva < section.virtual_address + section.virtual_size.max(section.raw_size)
            })
            .map(|section| (rva - section.virtual_address + section.raw_offset) as usize)
            .ok_or_else(|| format!("RVA {rva:#x} is not inside any section"))
    }
}

fn u16_at(data: &[u8], offset: usize) -> Result<u16, String> {
    data.get(offset..offset + 2)
        .map(|bytes| u16::from_le_bytes([bytes[0], bytes[1]]))
        .ok_or_else(|| format!("Unexpected end of file at {offset:#x}"))
}

fn u32_at(data: &[u8], offset: usize) -> Result<u32, String> {
    data.get(offset..offset + 4)
        .map(|bytes| u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
        .ok_or_else(|| format!("Unexpected end of file at {offset:#x}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Runs against a local game copy when `OPENFORTS_GAME_DIR` is set; skipped otherwise.
    #[test]
    fn patches_exactly_the_import_name() {
        let Ok(folder) = std::env::var("OPENFORTS_GAME_DIR") else {
            return;
        };
        let original = fs::read(Path::new(&folder).join(ORIGINAL_EXE)).unwrap();
        assert_eq!(sha256_hex(&original), GOG_WF_EXE_SHA256);
        let patched = patch_import_name(original.clone()).unwrap();
        let changed: Vec<usize> = (0..original.len())
            .filter(|&index| original[index] != patched[index])
            .collect();
        assert!(!changed.is_empty() && changed.len() <= RUNTIME_IMPORT.len());
        let first = changed[0];
        let start = original[..=first]
            .iter()
            .rposition(|&byte| byte == 0)
            .map_or(0, |index| index + 1);
        assert_eq!(
            &patched[start..start + RUNTIME_IMPORT.len() + 1],
            b"OFCRT.DLL\0"
        );
    }
}
