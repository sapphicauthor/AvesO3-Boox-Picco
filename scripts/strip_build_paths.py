# Keep the builder's home folder and project path out of the firmware image.
# Framework and library log macros embed __FILE__; without a prefix map those
# strings carry absolute paths such as /Users/<name>/.platformio/..., which a
# published release would expose. The map rewrites them to "~" and "." at
# compile time; the firmware's behaviour is unchanged.
import os

Import("env")

home = os.path.expanduser("~")
project = env.subst("$PROJECT_DIR")
flags = []
# Longest prefix first: the project usually lives under the home folder.
for prefix, replacement in sorted(((project, "."), (home, "~")), key=lambda p: -len(p[0])):
    if prefix:
        flags.append(f"-ffile-prefix-map={prefix}={replacement}")
# CCFLAGS reach the framework and every library built from this environment.
env.Append(CCFLAGS=flags)
