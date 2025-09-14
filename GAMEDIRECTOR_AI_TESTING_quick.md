# GameDirector AI — Quick Test Instructions (Judges)

Follow these steps after completing the **installation guide**.

---

## 1) Launch the Unreal Project
- Open your `.uproject`.
- Ensure the **GameDirector AI** plugin is enabled (Edit → Plugins).

## 2) Enable Log Filtering
- Open **Output Log**: `Window → Developer Tools → Output Log`.
- Click **Filters / Categories** (funnel icon).
- Enable category **`LogGameAI`**. (Optionally disable others to reduce noise.)

## 3) Play the Pre‑Loaded Level
- Click **Play** (PIE). You will see **three green “floor tile triggers.”**  
  - **Mission Tile:** Game Director AI **assigns a mission** to the player.  
  - **NPC Spawn Tile:** The AI **spawns an NPC**.  
  - **Weather Tile:** The AI **changes the weather**.

## 4) Trigger a Generation
- Walk onto any green tile. This **prompts the model** and starts a **JSON‑only response generation**.
- The **progress of generation** is visible in the **Output Log** under `LogGameAI` (you’ll see streamed chunks and the final JSON).

## 5) Reliability Note
- The generation is **not 100% reliable** on every attempt.  
  If a run stalls or returns invalid output, simply **try again** (step off and back onto the tile, or exit/enter PIE).

---

### What Success Looks Like
- Output Log shows `LogGameAI` lines with streaming and a final **valid JSON**.  
- The level reacts accordingly: **mission assigned**, **NPC spawned**, or **weather changed**.

### Quick Troubleshooting
- No `LogGameAI` output → Re‑enable the **Filters/Categories** for `LogGameAI`.  
- Plugin failed to load → Verify DLLs under `Plugins\GameDirectorPlugin\Binaries\Win64\` and unblock them.  
- Model not found → Ensure the `.gguf` file is in the **project root** (next to the `.uproject`).

Good luck and have fun! 🎮
