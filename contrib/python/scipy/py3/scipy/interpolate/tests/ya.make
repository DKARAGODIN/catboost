# Generated by devtools/yamaker.

PY3TEST()



VERSION(1.2.3)

ORIGINAL_SOURCE(mirror://pypi/s/scipy/scipy-1.2.3.tar.gz)

SIZE(MEDIUM)

FORK_TESTS()

PEERDIR(
    contrib/python/scipy/py3
    contrib/python/scipy/py3/scipy/conftest
)

NO_LINT()

NO_CHECK_IMPORTS()

TEST_SRCS(
    __init__.py
    test_bsplines.py
    test_fitpack.py
    test_fitpack2.py
    test_interpnd.py
    test_interpolate.py
    test_interpolate_wrapper.py
    test_ndgriddata.py
    test_pade.py
    test_polyint.py
    test_rbf.py
    test_regression.py
)

END()
