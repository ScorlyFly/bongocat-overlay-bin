import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG_EXAMPLE = (pathlib.Path(__file__).resolve().parents[1] / "config.ini.example").read_text()
DESKTOP = (pathlib.Path(__file__).resolve().parents[1] / "packaging" / "bongocat-overlay.desktop")
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

    def test_overlay_is_click_through_without_drag_area(self):
        self.assertIn("Qt.WindowTransparentForInput", QML)
        self.assertNotIn("MouseArea", QML)
        self.assertNotIn("positionBridge", QML)


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
        self.assertNotIn('QStringLiteral("Overlay/x")', MAIN_CPP)
        self.assertNotIn('QStringLiteral("Overlay/y")', MAIN_CPP)

    def test_size_config_is_supported(self):
        self.assertIn('sizeOption(QStringLiteral("size")', MAIN_CPP)
        self.assertIn('settings.value(QStringLiteral("Overlay/size")', MAIN_CPP)
        self.assertIn('QStringLiteral("100%")', MAIN_CPP)
        self.assertIn('setContextProperty(QStringLiteral("overlayWidth")', MAIN_CPP)
        self.assertIn('setContextProperty(QStringLiteral("overlayHeight")', MAIN_CPP)

    def test_config_template_contains_size_variable(self):
        self.assertIn('[Overlay]', CONFIG_EXAMPLE)
        self.assertIn('size=100%', CONFIG_EXAMPLE)

    def test_size_is_scaled_from_the_original_sprite_and_used_by_layer_shell(self):
        self.assertIn('kBaseOverlayWidth', MAIN_CPP)
        self.assertIn('kBaseOverlayHeight', MAIN_CPP)
        self.assertIn('qRound', MAIN_CPP)
        self.assertIn('setDesiredSize(QSize(overlayWidth, overlayHeight))', MAIN_CPP)

    def test_qml_uses_the_configured_overlay_size(self):
        self.assertIn('width: overlayWidth', QML)
        self.assertIn('height: overlayHeight', QML)

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

    def test_native_window_is_click_through(self):
        self.assertIn("Qt::WindowTransparentForInput", MAIN_CPP)
        self.assertNotIn("PositionBridge", MAIN_CPP)
        self.assertNotIn("dragTo", MAIN_CPP)
