import pytest
import sys

from .. import common

# Skip the event-loop heavy tests when the native module is missing or incomplete on
# the current platform.  This keeps CI environments without the compiled extension
# from failing during collection.
yup = pytest.importorskip(
    "yup", reason="yup native module is not available for event binding tests"
)

if sys.platform != "darwin" or not hasattr(yup, "MessageManager") or not hasattr(yup, "YUPApplication"):
    pytest.skip(allow_module_level=True)
