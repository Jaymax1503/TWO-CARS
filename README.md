# TWO-CARS

A terminal-based arcade game where you control two cars at the same time. Dodge obstacles, collect coins, and survive as long as you can!

## 🕹️ Controls

| Action | Left Car | Right Car |
| :--- | :--- | :--- |
| **Move Left** | `A` | `J` |
| **Move Right** | `D` | `L` |

* **Pause:** `P`
* **Quit:** `Q`
* **Restart:** `R` (on Game Over screen)

## ✨ Features

* **Coins (O):** Gain +1 point.
* **Obstacles (#):** Game over if you hit one!
* **Invincibility (*):** Smash through obstacles for a short time.
* **Magnet (M):** Pulls nearby coins into your lane automatically.
* **Difficulty:** The game gets faster every 20 points.
* **High Score:** Saves your best score automatically.

## 🛠️ How to Install

You need the `ncurses` library to play.

**On Linux (Ubuntu/Debian):**
```bash
sudo apt-get install libncurses5-dev libncursesw5-dev
```
To play the game, either download and extract the compressed zip directly from the GitHub page, or `gh repo clone Jaymax1503/TWO-CARS` if using GitHub terminal client, or clone [https://github.com/Jaymax1503/TWO-CARS](https://github.com/Jaymax1503/TWO-CARS)  
To **run** the game:
```bash
  make
./game
```
The instructions to play the game are very simple and clearly mentioned at the start of the game.


