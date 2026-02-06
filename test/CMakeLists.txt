# TLED Unit Tests
# Run with: idf.py -T test build flash monitor

idf_component_register(SRC_DIRS "."
                       PRIV_INCLUDE_DIRS "." "../main"
                       PRIV_REQUIRES unity)
