# BetteRCon
BetteRCon is a cross-platform Battlefield Bad Company 2 / Battlefield 3 / Battlefield 4 / Battlefield Hardline RCON client/relay server. It is inspired by PRoCon, but uses modern C++ and synchronization techniques to produce unparalleled performance. It is currently under development and has no official release yet. The master branch is not considered stable but will remain much more stable than the dev branch, and major official releases will be API compatible. Over time, BetteRCon-compatible versions of PRoCon plugins will be released as a direct replacement with identical functionality.

## BetteRConFramework
BetteRConFramework provides a variable-abstraction toolset for remote management of Frostbite gameservers. It allows for raw communication using Connection, that abstracts away the packets, and an enclosed Server with a full plugin API and event handling system.

## BetteRConConsole
BetteRConConsole's purpose at the time of writing is to run plugins that will respond to in-game events and interface with players.

## BetteRConSamplePlugin
BetteRConSamplePlugin is a barebones sample plugin whose purpose is to demonstrate the basic structure of a plugin, including simple identification, to adding timed actions, and installing event handlers.

## FastRoundStart
FastRoundStart is a practical plugin example designed to hook new round events, and set a timed event to run the next round after 30 seconds.

## Assist
Assist allows players to assist the weak team with an in-game command, with the following conditions:
- The player's team is winning
- The player's team is holding ground and owns the majority of flags
- The player's team's strength is within 50% of the enemy's strength. Strength is calculated with these variables:
  - Relative K/D Ratio (to teammates)
  - Relative Kills per Round (to teammates)
  - Relative Score per Round (to teammates)
  - Win/Loss Ratio
  
and are scaled based on round playtime. Strength is calculated with the formula (relativeKDR / 2) * (relativeKPR / 2) * (relativeSPR / 2) * (winLossRatio * 4)
The one caveot is that it currently only works for Conquest Small and Large. It has not been adapted for other modes yet, but may be in the future.
