import js from "@eslint/js";
import prettier from "eslint-config-prettier";
import { defineConfig } from "eslint/config";
import unicorn from "eslint-plugin-unicorn";
import globals from "globals";
import tseslint from "typescript-eslint";

export default defineConfig(
  { ignores: ["dist", "src-tauri"] },
  js.configs.recommended,
  tseslint.configs.strictTypeChecked,
  tseslint.configs.stylisticTypeChecked,
  unicorn.configs.recommended,
  {
    languageOptions: {
      globals: globals.browser,
      parserOptions: {
        projectService: { allowDefaultProject: ["*.js", "*.ts"] },
        tsconfigRootDir: import.meta.dirname,
      },
    },
  },
  // The config file itself is plain JavaScript outside the TypeScript project.
  {
    files: ["eslint.config.js"],
    extends: [tseslint.configs.disableTypeChecked],
  },
  prettier,
);
