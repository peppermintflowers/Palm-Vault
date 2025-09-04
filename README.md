# Palm Vault
This project is a gesture-based lock system for the STM32F429I-DISC development board. It allows a user to set and enter a secret sequence of hand gestures to unlock the device.

### Modes of Operation

**Enter-Key Mode:** Activated with a short button press. Use this to unlock the system by performing your saved gesture sequence.

**Record-Key Mode:** Activated with a long button press (press and hold > 1 second). Use this to set or change your secret gesture sequence.

### How It Works

#### Record-Key Mode:

Follow the LCD instructions to perform 5 hand gestures in a sequence.

This sequence becomes your personal gesture key.
     

#### Enter-Key Mode:

Perform your saved gesture sequence to unlock the system.

Correct sequence: LCD shows "UNLOCKED".

Incorrect sequence: LCD shows "TRY AGAIN".

**Team Members: Afsara Khan, Sohani Patki and Venesa Gomes**
