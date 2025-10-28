idf_component_register(
  SRCS "camera_setup.c"                     # sources in this component
  INCLUDE_DIRS "include"             # public headers exported to dependents
  REQUIRES esp32-camera          # PUBLIC deps (propagate to dependents)
#   PRIV_REQUIRES freertos             # PRIVATE deps (only for this component)
)