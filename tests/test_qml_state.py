import pathlib
import re
import struct
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG_EXAMPLE = (pathlib.Path(__file__).resolve().parents[1] / "config.ini.example").read_text()
DESKTOP = (ROOT / "packaging" / "bongocat-overlay.desktop")
if not DESKTOP.is_file():
    DESKTOP = ROOT / "packaging" / "bongocat-overlay"
ICON = (pathlib.Path(__file__).resolve().parents[1] / "packaging" / "bongocat-overlay.svg")
GITIGNORE = (pathlib.Path(__file__).resolve().parents[1] / ".gitignore")
QML = (pathlib.Path(__file__).resolve().parents[1] / "Overlay.qml").read_text()
MAIN_CPP = (pathlib.Path(__file__).resolve().parents[1] / "main.cpp").read_text()


class OverlayStateContractTest(unittest.TestCase):
    def test_mouse_buttons_do_not_trigger_keyboard_paws(self):
        self.assertIn('function onMouseButton(button) { /* mouse buttons never drive keyboard paws */ }', QML)

    def test_complete_keyboard_frame_state_machine_is_preserved(self):
        for frame in ("bongo-cat-both-up.png", "bongo-cat-left-down.png",
                      "bongo-cat-right-down.png", "bongo-cat-both-down.png"):
            self.assertIn(frame, QML)

    def test_overlay_has_a_drag_area(self):
        self.assertIn("MouseArea", QML)
        self.assertIn("positionBridge", QML)
        self.assertIn("dragBy", QML)
        self.assertIn("root.selected = !root.selected", QML)
        self.assertNotIn("Qt.WindowTransparentForInput", QML)
        # QQuickMouseEvent (the MouseArea signal argument) has no scenePosition.
        self.assertNotIn("scenePosition", QML)

    def test_drag_compensates_for_the_window_moving_under_the_pointer(self):
        # mouse.x/y are window-local: once the surface moves, the local origin
        # shifts by the applied distance. Without compensation each next delta
        # is wrong and dragging stutters.
        self.assertIn("const applied = positionBridge.dragBy(dx, dy)", QML)
        self.assertIn("lastPointerX = mouse.x - applied.x", QML)
        self.assertIn("lastPointerY = mouse.y - applied.y", QML)

    def test_bottom_right_resize_grip_appears_with_the_frame(self):
        self.assertIn('id: resizeGrip', QML)
        self.assertIn('visible: root.selected', QML)
        self.assertIn('assets/resize-grip.png', QML)
        self.assertIn('positionBridge.resizeBy(2,', QML)
        self.assertIn('positionBridge.finishResize()', QML)
        self.assertIn('Qt.SizeFDiagCursor', QML)
        self.assertIn('Q_INVOKABLE QPoint resizeBy', MAIN_CPP)
        self.assertNotIn('positionBridge.resizeTo(', QML)
        self.assertTrue((ROOT / "assets" / "resize-grip.png").is_file())
        self.assertIn('assets/resize-grip.png', (ROOT / "CMakeLists.txt").read_text())


if __name__ == "__main__":
    unittest.main()

class InputRenderContractTest(unittest.TestCase):
    def test_mouse_position_is_bound_to_target(self):
        self.assertIn('handX = targetHandX', QML)
        self.assertIn('handY = targetHandY', QML)

    def test_position_config_is_supported(self):
        self.assertIn('positionOption(QStringLiteral("position")', MAIN_CPP)
        self.assertIn('settings.value(QStringLiteral("Overlay/position")', MAIN_CPP)
        self.assertIn('QStringLiteral("Overlay/position")', MAIN_CPP)
        self.assertIn('QStringLiteral("Overlay/x")', MAIN_CPP)
        self.assertIn('QStringLiteral("Overlay/y")', MAIN_CPP)
        self.assertIn('QStringLiteral("custom")', MAIN_CPP)

    def test_size_config_is_supported(self):
        self.assertIn('sizeOption(QStringLiteral("size")', MAIN_CPP)
        self.assertIn('settings.value(QStringLiteral("Overlay/size")', MAIN_CPP)
        self.assertIn('QStringLiteral("100%")', MAIN_CPP)
        self.assertIn('Q_PROPERTY(int overlayWidth', MAIN_CPP)
        self.assertIn('Q_PROPERTY(int overlayHeight', MAIN_CPP)

    def test_config_template_contains_size_variable(self):
        self.assertIn('[Overlay]', CONFIG_EXAMPLE)
        self.assertIn('size=100%', CONFIG_EXAMPLE)

    def test_size_is_scaled_from_the_original_sprite_and_used_by_layer_shell(self):
        self.assertIn('kBaseOverlayWidth', MAIN_CPP)
        self.assertIn('kBaseOverlayHeight', MAIN_CPP)
        self.assertIn('qRound', MAIN_CPP)
        self.assertIn('setDesiredSize(m_overlaySize)', MAIN_CPP)

    def test_sprites_are_cropped_to_the_overlay_size_without_transparent_padding(self):
        # Every sprite canvas must match the window base size. Otherwise the
        # transparent border keeps the visible cat away from the screen edge.
        base_width = int(re.search(r'kBaseOverlayWidth\s*=\s*(\d+)', MAIN_CPP).group(1))
        base_height = int(re.search(r'kBaseOverlayHeight\s*=\s*(\d+)', MAIN_CPP).group(1))
        for name in ("bongo-cat-both-up.png", "bongo-cat-left-down.png",
                     "bongo-cat-right-down.png", "bongo-cat-both-down.png"):
            data = (ROOT / "assets" / name).read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            width, height = struct.unpack(">II", data[16:24])
            self.assertEqual((width, height), (base_width, base_height), name)

    def test_qml_uses_the_configured_overlay_size(self):
        self.assertIn('width: positionBridge.overlayWidth', QML)
        self.assertIn('height: positionBridge.overlayHeight', QML)

    def test_clicking_selects_the_cat_and_shows_a_resize_outline(self):
        self.assertIn('property bool selected: false', QML)
        self.assertIn('id: selectionOutline', QML)
        self.assertIn('visible: root.selected', QML)
        self.assertIn('border.width: 2', QML)
        self.assertIn('root.selected = !root.selected', QML)

    def test_selection_outline_matches_the_visible_cat_body(self):
        self.assertIn('x: root.width * positionBridge.contentLeft', QML)
        self.assertIn('y: root.height * positionBridge.contentTop', QML)
        self.assertIn('width: root.width * positionBridge.contentWidth', QML)
        self.assertIn('height: root.height * positionBridge.contentHeight', QML)
        self.assertIn('Q_PROPERTY(qreal contentLeft', MAIN_CPP)
        self.assertIn('Q_PROPERTY(qreal contentWidth', MAIN_CPP)
        self.assertIn('kContentLeftPx', MAIN_CPP)
        self.assertIn('kContentWidthPx', MAIN_CPP)
        # The frames differ in shape; keep the sprite at its natural aspect
        # ratio so the content rectangle still lines up with the cat.
        self.assertIn('fillMode: Image.PreserveAspectFit', QML)

    def test_resize_grip_sits_on_the_bottom_right_corner(self):
        self.assertIn('selectionOutline.x + selectionOutline.width - 6', QML)
        self.assertIn('selectionOutline.y + selectionOutline.height - 6', QML)

    def test_only_the_bottom_right_corner_resizes_the_cat(self):
        self.assertNotIn('ResizeHandle {', QML)
        self.assertNotIn('corner: 0', QML)
        self.assertNotIn('corner: 1', QML)
        self.assertNotIn('corner: 3', QML)
        self.assertIn('positionBridge.resizeBy(2,', QML)
        self.assertIn('positionBridge.finishResize()', QML)
        self.assertIn('Overlay/size', MAIN_CPP)

    def test_mirror_button_is_shown_with_the_selection_frame(self):
        self.assertIn('id: mirrorButton', QML)
        self.assertIn('visible: root.selected', QML)
        self.assertIn('onClicked: positionBridge.mirrored = !positionBridge.mirrored', QML)
        self.assertIn('text: "\\u2194"', QML)

    def test_close_button_quits_the_application(self):
        self.assertIn('id: closeButton', QML)
        self.assertIn('visible: root.selected', QML)
        self.assertIn('text: "\\u00d7"', QML)
        self.assertIn('onClicked: positionBridge.quit()', QML)
        self.assertIn('Q_INVOKABLE void quit()', MAIN_CPP)
        self.assertIn('QCoreApplication::quit()', MAIN_CPP)

    def test_mirroring_flips_around_the_visible_body(self):
        self.assertIn('transform: Scale {', QML)
        self.assertIn('positionBridge.contentLeft + positionBridge.contentWidth / 2', QML)
        self.assertIn('xScale: positionBridge.mirrored ? -1 : 1', QML)

    def test_mirror_state_is_persisted(self):
        self.assertIn('Q_PROPERTY(bool mirrored READ mirrored WRITE setMirrored', MAIN_CPP)
        self.assertIn('QStringLiteral("Overlay/mirror")', MAIN_CPP)
        self.assertIn('m_mirrored = m_settings->value', MAIN_CPP)

    def test_desktop_launcher_declares_the_standalone_application(self):
        desktop = DESKTOP.read_text()
        self.assertIn('[Desktop Entry]', desktop)
        self.assertIn('Type=Application', desktop)
        self.assertIn('Name=Bongo Cat', desktop)
        self.assertIn('Exec=bongocat-overlay', desktop)
        self.assertIn('Icon=bongocat-overlay', desktop)
        self.assertIn('Terminal=false', desktop)
        self.assertIn('Categories=Utility;', desktop)

    def test_scalable_icon_is_present(self):
        icon = ICON.read_text()
        self.assertIn('<svg', icon)
        self.assertIn('viewBox=', icon)
        self.assertIn('</svg>', icon)

    def test_generated_files_are_ignored(self):
        gitignore = GITIGNORE.read_text()
        for pattern in ('build/', '__pycache__/', '*.pyc', '.cache/',
                        '.config/bongocat-overlay/'):
            self.assertIn(pattern, gitignore)

    def test_cmake_installs_desktop_icon_and_config(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn('packaging/bongocat-overlay.desktop', cmake)
        self.assertIn('share/applications', cmake)
        self.assertIn('packaging/bongocat-overlay.svg', cmake)
        self.assertIn('share/icons/hicolor/scalable/apps', cmake)
        self.assertIn('config.ini.example', cmake)
        self.assertNotIn('CatImage.qml', cmake)
        self.assertNotIn('helper/input-access', cmake)
        self.assertNotIn('bongo-cat-mouse-base.png', cmake)
        self.assertNotIn('bongo-cat-mouse-hand.png', cmake)
        self.assertIn('assets/bongo-cat-both-up.png', cmake)

    def test_standalone_readme_documents_launcher_and_size(self):
        readme = (ROOT / "README.md").read_text()
        self.assertIn('share/applications', readme)
        self.assertIn('~/.config/bongocat-overlay/config.ini', readme)
        self.assertIn('size=75%', readme)
        self.assertIn('docs/screenshots/bongocat-overlay.png', readme)
        for name in ('BarWidget.qml', 'Service.qml', 'manifest.json'):
            self.assertNotIn(name, readme)
        self.assertIn('не выводятся', readme)

    def test_readme_screenshot_is_checked_in(self):
        self.assertTrue((ROOT / "docs" / "screenshots" / "bongocat-overlay.png").is_file())

    def test_standalone_build_does_not_reference_plugin_files(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        for name in ("BarWidget.qml", "Service.qml", "manifest.json"):
            self.assertNotIn(name, cmake)

    def test_unused_plugin_and_mouse_files_are_removed(self):
        for relative_path in (
            "BarWidget.qml", "Service.qml", "manifest.json", "README-STANDALONE.md",
            "CatImage.qml", "preview.png",
            "assets/bongo-cat-mouse-base.png", "assets/bongo-cat-mouse-hand.png",
        ):
            self.assertFalse((ROOT / relative_path).exists(), relative_path)

    def test_input_debug_does_not_log_raw_keycodes(self):
        helper = (ROOT / "helper" / "bongo-input").read_text()
        self.assertNotIn('print(f"EVDEV {descriptors.get(fd).value} {keycode}', helper)
        self.assertIn('record.split(chr(9), 1)[0]', helper)

    def test_all_grid_positions_are_supported(self):
        for position in ("up-left", "up", "up-right", "left", "center",
                         "right", "down-left", "down", "down-right"):
            self.assertIn('QStringLiteral("' + position + '")', MAIN_CPP)

    def test_native_window_supports_dragging(self):
        self.assertNotIn("Qt::WindowTransparentForInput", MAIN_CPP)
        self.assertIn("PositionBridge", MAIN_CPP)
        self.assertIn("dragBy", MAIN_CPP)
        self.assertIn("finishDrag", MAIN_CPP)
        self.assertIn("AnchorTop", MAIN_CPP)
        self.assertIn("AnchorLeft", MAIN_CPP)
