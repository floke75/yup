import pytest

from .. import common

# Skip graphics-oriented tests when the native bindings are not present.  The Python
# stubs used in documentation builds do not expose the rendering API, so we avoid
# running the suite entirely in that configuration.
yup = pytest.importorskip(
    "yup", reason="yup native module is not available for graphics binding tests"
)

# Bail out early when the module lacks the modern Graphics façade to avoid a cascade
# of AttributeErrors in downstream fixtures.
if not hasattr(yup, "Graphics"):
    pytest.skip(allow_module_level=True)
