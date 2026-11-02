#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
classes="$(mktemp -d)"
trap 'rm -rf "$classes"' EXIT
javac_cmd="${JAVA_HOME:+$JAVA_HOME/bin/}javac"
java_cmd="${JAVA_HOME:+$JAVA_HOME/bin/}java"
"$javac_cmd" -d "$classes" \
    "$root/app/src/main/java/org/supermariowar/app/TouchInput.java" \
    "$root/app/src/main/java/org/supermariowar/app/TouchLayout.java" \
    "$root/app/src/main/java/org/supermariowar/app/TouchRouter.java" \
    "$root/app/src/main/java/org/supermariowar/app/TouchKeys.java" \
    "$root/app/src/main/java/org/supermariowar/app/PadManager.java" \
    "$root/tests/TouchControlsTest.java" \
    "$root/tests/PadManagerTest.java"
"$java_cmd" -cp "$classes" org.supermariowar.app.TouchControlsTest
"$java_cmd" -cp "$classes" org.supermariowar.app.PadManagerTest
