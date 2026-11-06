// usb_names.c - the name the controller shows up with on the computer
// (Ableton's MIDI preferences, Audio MIDI Setup, the config tool).
#include "usb_names.h"

#define MIDI_NAME     {'M','P','M','O','T','H','E','R'}
#define MIDI_NAME_LEN 8

struct usb_string_descriptor_struct usb_string_product_name = {
  2 + MIDI_NAME_LEN * 2,
  3,
  MIDI_NAME
};
