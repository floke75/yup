import pytest

from .. import common

# Ensure the native `yup` module is available before running the core bindings tests.
# The CI environment used for documentation-only changes does not build the extension,
# so we skip this test package entirely when the import fails.
yup = pytest.importorskip(
    "yup", reason="yup native module is not available for core binding tests"
)
