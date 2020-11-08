Homepage: http://vrokolos.blogspot.com/2008/03/change-speaker-mode-using-command-line.html

This program changes the speaker settings under Control Panel -> Sound and Audio
 devices -> Advanced -> Speaker Settings using command line.

Useage: speakersetup.exe [Speaker Mode]

[Speaker Mode] can be one of the following:
     hp: Stereo headphones
      1: Monophonic speaker
      2: Desktop stereo speakers (2.0)
      4: Quadraphonic speakers (4.0)
      5: Surround sound speakers (5.0)
    5.1: 5.1 surround sound speakers
    7.1: 7.1 speakers

Examples:
    "speakersetup 5.1" will switch to 5.1 surround sound speakers.
    "speakersetup hp" will switch to headphones.