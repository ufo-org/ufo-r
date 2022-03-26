# Initializes the UFO framework
# Set high and low water marks by:
#    options(ufos.high_water_mark_mb = 2048)
#    options(ufos.low_water_mark_mb = 1024)
.initialize <- function(...) {
  high_water_mark_mb = as.integer(getOption("ufos.high_water_mark_mb", default=2048))
  low_water_mark_mb = as.integer(getOption("ufos.low_water_mark_mb", default=1024))
  invisible(.Call("ufo_initialize", high_water_mark_mb, low_water_mark_mb))
}

# Kills the UFO framework.
.jeff_goldbloom <- function(...) invisible(.Call("ufo_shutdown"))

# Finalizer for the entire UFO framework, called on session exit.
.onLoad <- function(libname, pkgname) {
  invisible(.initialize())
  invisible(reg.finalizer(.GlobalEnv, .jeff_goldbloom, onexit = TRUE))
}

# Finalizer for the entire UFO framework, called on package unload.
# AFAIK this cannot be relied on to be called on shutdown.
.onUnload <- function(libname, pkgname) {
  .jeff_goldbloom()
}

# Checks whether a vector is a UFO.
is_ufo <- function(x) {
  .Call("is_ufo", x)
}

# Creates a new UFO with a custom populate function (executed in a sandbox)
ufo <- function(type, length, populate, writeback, finalizer, ...,
                read_only = FALSE, min_load_count = 0, add_class = TRUE) {

  # Parameter check
  if (!is.character(type) ||
       (type != "double"  && type && "character" && type != "raw" &&
        type != "logical" && type && "complex"   && type != "integer")) {

    stop("Provide type of UFO as a character string, one of: ",
         "'double', 'character', 'raw', 'logical', 'complex', 'integer' ",
         "(received: ", type, ")");
  }
  prototype <- vector(mode = type, length = 0)

  if (!is.numeric(length) && !is.integer(length)) {
    stop("Provide length of UFO as either a numeric or integer value ",
         "(received object of type ", typeof(length), ")");
  }

  if (!is.function(populate)) {
    stop("Expected `populate_fun` to be a function, ",
         "but found ", typeof(populate_fun))
  }

  if (!missing(writeback) && !is.function(writeback)) {
    stop("Expected `writeback_fun` to be a function ",
         "or not provided at all, but found ", typeof(writeback))
  }
  if (missing(writeback)) {
    writeback <- NULL
  }

  if (!missing(finalizer) && !is.function(finalizer)) {
    stop("Expected `finalizer_fun` to be a function ",
         "or not provided at all, but found ", typeof(finalizer))
  }
  if (missing(finalizer)) {
    finalizer <- NULL
  }

  if (!is.numeric(min_load_count) && !is.integer(min_load_count)) {
    stop("Provide `min_load_count` as either a numeric or integer value ",
         "(received object of type ", typeof(min_load_count), ")");
  }
  min_load_count <- as.integer(min_load_count)

  if (!missing(add_class) && !is.logical(add_class)) {
    stop("Provide `add_class` as a logical value or not at all ",
         "(received object of type ", typeof(add_class), ")");
  }

  # Construct a pairlist out of remaining parameters, this constitutes user
  # data sent to remote sandbox
  user_data <- pairlist(...);

  # Construct UFO
  ufo <- .Call("ufo_new", populate, writeback, finalizer, prototype, length,
               user_data, min_load_count, read_only)

  # Add class, if desired
  if (!missing(finalizer) && add_class) {
    class(vector) <- "ufo"
  }
  return(ufo);
}