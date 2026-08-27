import importlib.util
import importlib.machinery
import pathlib
import unittest

HELPER = pathlib.Path(__file__).resolve().parents[1] / "helper" / "bongo-input"
spec = importlib.util.spec_from_loader("bongo_input", importlib.machinery.SourceFileLoader("bongo_input", str(HELPER)))
assert spec and spec.loader
bongo_input = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bongo_input)


class EventRoutingTest(unittest.TestCase):
    def test_keyboard_a_becomes_left_paw(self):
        self.assertEqual(
            bongo_input.event_record(bongo_input.DeviceKind.KEYBOARD,
                                     bongo_input.EV_KEY, bongo_input.KEY_A, 1),
            "L",
        )

    def test_keyboard_k_becomes_right_paw(self):
        self.assertEqual(
            bongo_input.event_record(bongo_input.DeviceKind.KEYBOARD,
                                     bongo_input.EV_KEY, 37, 1),
            "R",
        )

    def test_mouse_button_never_becomes_keyboard_paw(self):
        self.assertEqual(
            bongo_input.event_record(bongo_input.DeviceKind.MOUSE,
                                     bongo_input.EV_KEY, bongo_input.BTN_LEFT, 1),
            "B\t272",
        )
        self.assertIsNone(
            bongo_input.event_record(bongo_input.DeviceKind.MOUSE,
                                     bongo_input.EV_KEY, bongo_input.KEY_A, 1),
        )

    def test_mouse_relative_motion_is_only_emitted_by_mouse(self):
        self.assertEqual(
            bongo_input.event_record(bongo_input.DeviceKind.MOUSE,
                                     bongo_input.EV_REL, bongo_input.REL_X, 5),
            "M\t5\t0",
        )
        self.assertIsNone(
            bongo_input.event_record(bongo_input.DeviceKind.KEYBOARD,
                                     bongo_input.EV_REL, bongo_input.REL_X, 5),
        )


if __name__ == "__main__":
    unittest.main()
