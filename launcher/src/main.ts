import { invoke } from "@tauri-apps/api/core";

interface GameStatus {
  exeFound: boolean;
  supported: boolean;
  sha256: string | null;
}

type ControlsProfile = "modern" | "classic";

interface Settings {
  debug: boolean;
  controls: ControlsProfile;
}

function isControlsProfile(value: string): value is ControlsProfile {
  return value === "modern" || value === "classic";
}

function element<T extends HTMLElement>(
  selector: string,
  type: new () => T,
): T {
  const found = document.querySelector(selector);
  if (!(found instanceof type)) {
    throw new TypeError(`Missing element ${selector}`);
  }
  return found;
}

const launchButton = element("#launch-button", HTMLButtonElement);
const statusText = element("#status", HTMLParagraphElement);
const debugCheckbox = element("#debug-mode", HTMLInputElement);
const controlsSelect = element("#controls-profile", HTMLSelectElement);

function showStatus(message: string, isError = false): void {
  statusText.textContent = message;
  statusText.classList.toggle("error", isError);
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function describe(status: GameStatus): string {
  if (!status.exeFound) {
    return "Worms Forts was not found. Put OpenForts Classic into your Worms Forts folder (the folder that contains WF.exe) and start it from there.";
  }
  if (!status.supported) {
    return `This WF.exe is not supported (SHA-256 ${status.sha256 ?? "unknown"}). Only the GOG version is supported for now.`;
  }
  return "Ready to play.";
}

async function checkGame(): Promise<void> {
  try {
    const status = await invoke<GameStatus>("game_status");
    showStatus(describe(status), !status.supported);
    launchButton.disabled = !status.supported;
  } catch (error) {
    showStatus(errorMessage(error), true);
  }
}

async function launch(): Promise<void> {
  launchButton.disabled = true;
  showStatus("Starting the game…");
  try {
    await invoke("launch");
    showStatus("The game is running.");
  } catch (error) {
    showStatus(errorMessage(error), true);
  } finally {
    launchButton.disabled = false;
  }
}

async function loadSettings(): Promise<void> {
  try {
    const settings = await invoke<Settings>("load_settings");
    debugCheckbox.checked = settings.debug;
    controlsSelect.value = settings.controls;
    debugCheckbox.disabled = false;
    controlsSelect.disabled = false;
  } catch (error) {
    showStatus(errorMessage(error), true);
  }
}

async function saveSettings(): Promise<void> {
  const controls = controlsSelect.value;
  if (!isControlsProfile(controls)) {
    return;
  }
  const settings: Settings = { debug: debugCheckbox.checked, controls };
  try {
    await invoke("save_settings", { settings });
  } catch (error) {
    showStatus(errorMessage(error), true);
    // Show what is actually saved.
    await loadSettings();
  }
}

launchButton.addEventListener("click", () => void launch());
debugCheckbox.addEventListener("change", () => void saveSettings());
controlsSelect.addEventListener("change", () => void saveSettings());
void checkGame();
void loadSettings();
