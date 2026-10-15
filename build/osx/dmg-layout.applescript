-- Only the disposable mounted installer; no global Finder settings or App launch.
on run arguments
    set mountPath to item 1 of arguments
    if mountPath does not start with "/tmp/wiznote-dmg." then error "Unexpected installer mount"
    if mountPath does not end with "/mounted" then error "Unexpected installer mount"
    set installer to POSIX file mountPath as alias
    tell application "Finder"
        open installer
        tell folder installer
            set current view of container window to icon view
            set toolbar visible of container window to false
            set statusbar visible of container window to false
            set pathbar visible of container window to false
            set bounds of container window to {180, 140, 860, 602}
            set options to icon view options of container window
            set arrangement of options to not arranged
            set icon size of options to 80
            set text size of options to 12
            set label position of options to bottom
            set shows item info of options to false
            set background picture of options to file ".background:install.png"
            set position of item "WizNotePlus Apple Silicon.app" to {160, 170}
            set position of item "Applications" to {520, 170}
            set position of item "安装说明.txt" to {160, 330}
            set position of item "说明与源码" to {520, 330}
            update without registering applications
            close container window
        end tell
    end tell
end run
