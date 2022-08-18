# LwM2M Utilities

This module provides wrapper functions to common LwM2M engine functions.  Using snprintk it generates the path.

The Object Agent (linked list) that it provides allow LwM2M [sensor] objects to register a creation callback that occurs when a new object instance is enabled.

When object management is enabled (Gateway Objects must be enabled) it will create and delete objects as required.
