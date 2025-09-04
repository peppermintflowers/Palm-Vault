# Palm Vault
This is a sentry application to be run on the STM32F429I-DISC board.  

### Modes of Operation-
A short press puts the sentry in **enter-key mode**.  
A long press (>1s) puts the sentry in **record-key mode**.  

### What it does:
In the record-key mode, the user is expected to perform a series of 5 hand gestures, as guided by the LCD, to set the gesture key of the user's choice.  
In the enter-key mode, if the user performs the correct gesture sequence which has been set as key, the LCD flashes the message- "UNLOCKED", else prompts the user to "TRY AGAIN".  

**Team Members: Afsara Khan, Sohani Patki and Venesa Gomes**
