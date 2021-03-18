FIND_PATH(LIBAO_INCLUDE_DIR ao /opt/local/include /usr/include /usr/local/include)

FIND_LIBRARY(LIBAO_LIBRARIES NAMES ao PATH /opt/local/lib /usr/lib /usr/local/lib) 

IF (LIBAO_INCLUDE_DIR AND LIBAO_LIBRARIES)
  SET(LIBAO_FOUND TRUE)
ENDIF (LIBAO_INCLUDE_DIR AND LIBAO_LIBRARIES)

IF (LIBAO_FOUND)
   IF (NOT LIBAO_FIND_QUIETLY)
      MESSAGE(STATUS "Found libao: ${LIBAO_LIBRARIES}")
   ENDIF (NOT LIBAO_FIND_QUIETLY)
ELSE (LIBAO_FOUND)
   IF (LIBAO_FIND_REQUIRED)
      MESSAGE(FATAL_ERROR "Could not find libao")
   ENDIF (LIBAO_FIND_REQUIRED)
ENDIF (LIBAO_FOUND)
