# 🏆 PSP Achievements (ARK-4)

> Unlock achievements on real PSP hardware — completely offline.

A kernel-mode PRX plugin that brings [RetroAchievements](https://retroachievements.org/)-style trophies to the Sony PSP. It evaluates achievement logic in real-time by reading game RAM, with no network connection required.

---

## ✨ Features

| | |
|---|---|
| 🎯 **Real-Time Evaluation** | Custom logic parser runs alongside the game with zero lag |
| 🖼 **In-Game Popups** | Trophy-style notifications rendered directly to the framebuffer |
| 💾 **Tiny Footprint** | Runs smoothly even alongside heavy games like God of War |
| 🔍 **Auto-Detection** | Identifies the running game automatically |
| 📴 **Fully Offline** | No Wi-Fi, no server — everything runs locally |
| 💿 **Persistent Profile** | Progress saved to Memory Stick across sessions |

<details>
<summary><strong>Supported RA Logic</strong></summary>

- **Conditions:** Delta, Prior, HitCounts, ResetIf, PauseIf, AndNext, OrNext, AddSource, SubSource, AddAddress, ResetNextIf, Trigger, Measured, MeasuredIf
- **Groups:** Core + Alternate group evaluation
- **Memory Sizes:** 8-bit, 16-bit, 24-bit, 32-bit, Bit0–Bit7, Float LE, Float BE

</details>

---

## 🎮 Supported Games

<details>
<summary><strong>Click to expand game list</strong></summary>

| Game | Region | Game Code | Achievements | Status |
|------|--------|-----------|--------------|--------|
| God of War: Chains of Olympus | EUR | `UCES00842` | 39 | ✅ Working |
| Silent Hill: Origins | USA | `ULUS10285` | 28 | ✅ Working |

</details>

---

## 📥 Installation

1. Install **ARK-4** custom firmware on your PSP.
2. Download the latest release.
3. Copy files to your Memory Stick:

```
ms0:/
├── seplugins/
│   └── PspAchievements.prx
└── PSP/
    └── ACH/
        ├── game_map.dat
        └── games/
            ├── 3927.ach
            └── 26296.ach
```

4. Enable the plugin in ARK-4 Recovery Menu:  
   `game, ms0:/seplugins/PspAchievements.prx, on`
5. Launch your game and enjoy!

---

## 🛠 Building from Source

Requires [PSPSDK](https://github.com/pspdev/pspsdk).

```bash
cd plugin
make clean && make
```

---

## 🗺 Architecture

| Module | Description |
|--------|-------------|
| `main.c` | Thread management, game detection, main loop |
| `rcheevos_glue.c` | Core logic evaluator — parses RA syntax, tracks delta/prior states |
| `memory.c` | Safe kernel-mode RAM access via kseg0 with 25-bit address masking |
| `popup.c` | Direct framebuffer rendering without interrupting game graphics |
| `profile.c` | Achievement progress persistence to Memory Stick |
| `game_map.c` | UMD game code → achievement data file mapping |

---

## 🚀 Roadmap

- [x] Base RA logic parsing
- [x] Framebuffer popup rendering
- [x] Memory safety & RAM limits
- [x] Save/Load profile progress
- [x] Float (LE/BE) support
- [x] Bit-level memory reads (Bit0–Bit7)
- [x] ADD_ADDRESS pointer chains
- [x] Delta & Prior snapshot tracking
- [ ] Audio notification on unlock
- [ ] Support more games

---

## 🙏 Credits

- **[RetroAchievements](https://retroachievements.org/)** — Achievement definitions and community
- **[PPSSPP](https://www.ppsspp.org/)** — Emulator used for development and testing
- **[PSPSDK](https://github.com/pspdev/pspsdk)** — PSP development framework

---

## 📄 License

This project is provided for **educational purposes only**.

- 📚 Free to use for personal projects
- 🔧 Free to modify for learning
- ❌ Not for commercial use
- ⚠️ Use at your own risk
