# GameDirector AI — Installation Guide

This guide shows you how to install **GameDirector AI** from GitHub, copy the prebuilt DLLs, and place the language model (`.gguf`) in your Unreal project **root** (next to the `.uproject`).

> **Repo:** https://github.com/johdav999/GameDirectorAI  
> **Target platform:** Windows 10/11, Unreal Engine 5.6 (or 5.5), Visual Studio 2022 (for source builds if needed)

---

## 1) Prerequisites

- **Unreal Engine 5.6** (Epic Launcher or source)  
- **Visual Studio 2022** with “Desktop development with C++” workload (only needed if you compile from source)  
- **Git** (to clone the repo)

> If using a corporate/locked-down machine, ensure you can unzip to your project folder and run UE with plugin DLLs.

---

## 2) Clone the Plugin

Install the plugin into your own UE project under `Plugins/GameDirectorPlugin`:

```powershell
# In a PowerShell or CMD terminal
cd "C:\Path\To\YourUnrealProject\"
mkdir -Force .\Plugins\GameDirectorPlugin

# Clone the repo directly into the plugin folder
git clone https://github.com/johdav999/GameDirectorAI .\Plugins\GameDirectorPlugin
```

> If you already have a `GameDirectorPlugin` folder, back it up or remove it first.

---

## 3) Download the Prebuilt Binaries (DLLs)

If you **do not** want to compile the plugin, download the prebuilt Windows binaries and place them in the plugin `Binaries` folder.

**Link (Windows x64 binaries ZIP):**  
https://drive.google.com/file/d/1ACxPYuatUsLYJlCLmaugURu998CJKM_z/view?usp=sharing

1. Download the ZIP.  
2. Unzip it to:  
   ```text
   <YourProject>\Plugins\GameDirectorPlugin\Binaries\Win64\
   ```
   After unzipping, you should have the plugin DLL(s) under that `Win64` folder.

> If Windows warns about untrusted DLLs, right‑click → **Properties** → **Unblock** before launching UE.

---

## 4) Download the Model (GGUF) and Place It in the Project Root

**Link (GGUF model):**  
https://drive.google.com/file/d/1wu27aucfz-EEaVAl7z3Q24ZJzcfpacLd/view?usp=sharing

1. Download the `.gguf` file.  
2. Place it in the **project root** (the folder that contains your `.uproject`). Example:
   ```text
   <YourProject>\gptoss20b.f16pure.gguf
   ```

> The plugin expects the model at the project root by default. If you rename the file, update the code/config used in `LLamaRunnerAsync::Initiate(...)` accordingly.

---

## 5) Enable/Load the Plugin

1. Open your Unreal project (`.uproject`) in the Unreal Editor.  
2. Go to **Edit → Plugins** and find **GameDirector AI** (or **GameDirectorPlugin**).  
3. Ensure it’s **Enabled** (restart editor if prompted).

If everything is placed correctly, the plugin should load without missing‑DLL errors. If you see a load failure, double‑check the DLLs under:
```
<YourProject>\Plugins\GameDirectorPlugin\Binaries\Win64\
```

---

## 6) Quick Smoke Test (Blueprint)

1. Create a new **Actor Blueprint** (e.g., `BP_GDAITest`).  
2. In **Event BeginPlay**, call the exposed function (e.g., `GenerateJSON` or your test wrapper) with a short prompt, then **Print String** the result or **UE_LOG** to **Output Log**.  
3. **Play** in editor and confirm a JSON response appears.

> If you get an error like “model not found,” verify the `.gguf` path and that the file is in the project root. If you see decode failures on a second call, re‑run or consult the troubleshooting notes below.

---

## 7) (Optional) Build From Source

If you prefer building the plugin instead of using the provided DLLs:

1. Delete everything under `Plugins\GameDirectorPlugin\Binaries\` so Unreal regenerates binaries.  
2. Close the editor. Right‑click the `.uproject` → **Generate Visual Studio project files**.  
3. Open the solution and **Build** the project (Development Editor | Win64).  
4. Launch the editor. Unreal will compile and stage the plugin automatically.

> Make sure your include/lib paths for any third‑party dependencies (llama.cpp, etc.) are set up by the plugin’s `Build.cs` files.

---

## 8) Troubleshooting

- **“Plugin failed to load” / missing DLLs:**  
  Ensure the binaries are at `Plugins\GameDirectorPlugin\Binaries\Win64\*.dll`. Unblock DLLs if Windows flagged them.
- **Model not found / access denied:**  
  Confirm the `.gguf` is **next to the `.uproject`** and not inside `Content` or `Plugins`. If your filename differs, update the path in code/config.
- **First call works, second fails (decode error):**  
  Restart the editor for a quick fix, or ensure each generation uses a **unique sequence ID** (advanced), or re‑create the context between runs.
- **Large downloads blocked:**  
  If corporate filtering blocks Google Drive, use a direct link mirror or request an offline copy.

---

## 9) Contact

If a temporary link is unavailable or you hit an install block, please contact the team and we’ll provide an alternate delivery method for the binaries/model.

---

### Folder Layout (after setup)

```
YourProject\
├─ YourProject.uproject
├─ gptoss20b.f16pure.gguf          <-- model in project root
├─ Plugins\
│  └─ GameDirectorPlugin\
│     ├─ Binaries\
│     │  └─ Win64\
│     │     ├─ GameDirectorPlugin.dll
│     │     └─ (other required runtime DLLs)
│     ├─ Source\
│     └─ (other plugin files)
└─ Content\
```

Good luck with the evaluation! 🎮🧠
