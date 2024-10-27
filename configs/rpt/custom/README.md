
# ASL3 Node Menu Customization

## "/etc/asterisk/custom" directory

The "/etc/asterisk/custom" directory contains customizations to the ASL3 configuration files.
The base file and the optional customization(s) include :

| "/etc/asterisk" file | Can be customized / extended with
| -------------------- | ---------------------------------
| echolink.conf | custom/echolink.conf
| extensions.conf | custom/extensions.conf
| gps.conf | custom/gps.conf
| iax.conf | custom/iax.conf
| rpt.conf | custom/rpt.conf, custom/rpt/\*.conf
| simpleusb.conf | custom/simpleusb.conf, custom/simpleusb/\*.conf
| usbradio.conf | custom/usbradio.conf, custom/usbradio/\*.conf

## "rpt.conf" customizations

Customizations to "rpt.conf" are special.
In addition to the "custom/rpt.conf" file we also include ".conf" files in the "custom/rpt/" directory.
Each of these ".conf" files can include a comment line with the following format :

```
;MENU:keyed-gpio4:Assert GPIO (pin 4) when keyed
```

The ASL3 node setup menu looks for these specially formatted comment lines
to present customization options.  Each line consists of 3 fields separated by
":" characters.

| Field | Text | Description |
| :---: | :--: | :---------- |
| #1 | ;MENU | This field starts with a leading ";" (it's a comment) followed by the string "MENU"
| #2 | \<category> | This field names the category to be associate with the node
| #3 | \<description> | This field describes the node customization

## "simpleusb.conf" customizations

Customizations to "simpleusb.conf" are also special.
In addition to the "custom/simpleusb.conf" file we also include ".conf" files in the "custom/simpleusb/" directory.
As with the "rpt.conf" customizations, we also look for the specially formatted comment lines.
These customizations will be added for to the menu for any node using the "SimpleUSB" channel driver.

## "usbradio.conf" customizations

Customizations to "usbradio.conf" are also special.
In addition to the "custom/usbradio.conf" file we also include ".conf" files in the "custom/usbradio/" directory.
As with the "rpt.conf" customizations, we also look for the specially formatted comment lines.
These customizations will be added for to the menu for any node using the "SimpleUSB" channel driver.

## Changes to the "rpt.conf" configuration file

Some changes to the "rpt.conf" file are needed in order to support these menu customizations.

1. All of the "\[####]" node stanzas must be moved the end of the file.
2. Many of the "rpt.conf" stanzas were not setup for customization.  To allow these stanzas to be extended we added the "(!)" template marker to the following :

	- [controlstates]
	- [functions]
	- [macro]
	- [memory]
	- [morse]
	- [nodes]
	- [schedule]
	- [telemetry]
	- [wait-times]
	- [wait-times_hd]

## Using Asterisk templates

In ASL3, the configuration of each node is typically setup with the node (e.g. [63001]) inheriting from the [node-main] category and any settings overriding those in the template.
The ASL3 node setup customization menu adds to the list of categories inherited by the node.

Without any customizations, a node configuration might look like :

```
[63001](node-main)
idrecording = |iWB6NIL
```

With the "Assert GPIO (pin 4) when keyed" customization enabled the node configuration would become :

```
[63001](node-main,keyed-gpio4)
idrecording = |iWB6NIL
```

For more about templates, please refer to the Asterisk [Using Templates](https://docs.asterisk.org/Fundamentals/Asterisk-Configuration/Asterisk-Configuration-Files/Templates/Using-Templates/?h=template) documentation.

## Viewing a customized node configuration

The GitHub [ASL3 menu](https://github.com/AllStarLink/asl3-menu.git) project includes some EXPERIMENTAL (still in development) code that can be used to extract the expanded configuration for a node.
Assuming that you have already cloned the GitHub repository, you can use the following commands to view a node :

```bash
cd asl3-menu
./php-backend/asl-configuration.php --command=node_show --node=63001
```

The output will include the settings from the \[node-main] stanza, from any enabled customization stanzas, and any per-node overrides.
