#define USE_RINTERNALS

#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rallocators.h>

#include "ufos.h"
#include "R_ext.h"
//#include "mappedMemory/userfaultCore.h"
#include "ufo_c/target/ufo_c.h"

#include "make_sure.h"
#include "bad_strings.h"

UfoCore __ufo_system;
int __framework_initialized = 0;

typedef SEXP (*__ufo_specific_vector_constructor)(ufo_source_t*);

SEXP ufo_shutdown() {
    if (__framework_initialized) {
        __framework_initialized = 0;
        // Actual shutdown
        ufo_core_shutdown(__ufo_system);
    }
    return R_NilValue;
}

SEXP ufo_initialize(SEXP/*INTSXP*/ high_sexp, SEXP/*INTSXP*/ low_sexp) {
    if (!__framework_initialized) {
        __framework_initialized = 1;

    // ufo_begin_log(); // very verbose rust logging

        if(TYPEOF(high_sexp) != INTSXP) {
            Rf_error("Invalid type for high water mark: %s", type2char(TYPEOF(high_sexp)));
        }

        if(TYPEOF(low_sexp) != INTSXP) {
            Rf_error("Invalid type for high water mark: %s", type2char(TYPEOF(low_sexp)));
        }

        if (XLENGTH(high_sexp) == 0) {
            Rf_error("No high water mark value: vector of length zero");
        }

        if (XLENGTH(low_sexp) == 0) {
            Rf_error("No low water mark value: vector of length zero");
        }

        int high_mb = INTEGER_ELT(high_sexp, 0);
        int low_mb = INTEGER_ELT(low_sexp, 0);

        if (XLENGTH(high_sexp) > 1) {
            Rf_warning("Multiple values provided for high water mark value, using first one (%i)", high_mb);
        }

        if (XLENGTH(low_sexp) > 1) {
            Rf_warning("Multiple values provided for low water mark value, using first one (%i)", low_mb);
        }

        // Actual initialization
        size_t high = 1024L * 1024 * high_mb;
        size_t low = 1024 * 1024 * low_mb;

        __ufo_system = ufo_new_core("/tmp/", high, low);
        if (ufo_core_is_error(&__ufo_system)) {
            Rf_error("Error initializing the UFO framework");
        }
    }
    return R_NilValue;
}

void __validate_status_or_die (int status) {
    switch(status) {
        case 0: return;
        default: {
            // Print message for %i that depends on %i
            Rf_error("Could not create UFO (%i)", status);
        }
    }
}

uint32_t __get_stride_from_type_or_die(ufo_vector_type_t type) {
    switch(type) {
        case UFO_CHAR: return strideOf(Rbyte);
        case UFO_LGL:  return strideOf(Rboolean);
        case UFO_INT:  return strideOf(int);
        case UFO_REAL: return strideOf(double);
        case UFO_CPLX: return strideOf(Rcomplex);
        case UFO_RAW:  return strideOf(Rbyte);
        case UFO_STR:  return strideOf(SEXP);
        case UFO_VEC:  return strideOf(SEXP);
        default:       Rf_error("Cannot derive stride for vector type: %d\n", type);
    }
}

void* __ufo_alloc(R_allocator_t *allocator, size_t size) {
    ufo_source_t* source = (ufo_source_t*) allocator->data;
    
    // Make a duplicate of source for writeback.
    ufo_source_t* writeback_source = (ufo_source_t*) malloc(sizeof(ufo_source_t));
    writeback_source = memcpy(writeback_source, source, sizeof(ufo_source_t));

    size_t sexp_header_size = sizeof(SEXPREC_ALIGN);
    size_t sexp_metadata_size = sizeof(R_allocator_t);

    make_sure((size - sexp_header_size - sexp_metadata_size) >= (source->vector_size *  source->element_size), Rf_error,
    		  "Sizes don't match at ufo_alloc (%li vs expected %li).", size - sexp_header_size - sexp_metadata_size,
			  	  	  	  	  	  	  	  	  	  	  	  	  	  	   source->vector_size *  source->element_size);
    UfoParameters params = {
        .header_size = sexp_header_size + sexp_metadata_size,
        .element_size = __get_stride_from_type_or_die(source->vector_type),
        .min_load_ct = source->min_load_count,
        .read_only = source->read_only,
        .element_ct = source->vector_size,
        .populate_data = source->data,
        .populate_fn = source->population_function,
        .writeback_listener = source->writeback_function,
        .writeback_listener_data = source->data,
    };

    UfoObj object = ufo_new_object(&__ufo_system, &params);

    if (ufo_is_error(&object)) {
        Rf_error("Could not create UFO");
    }

    return ufo_header_ptr(&object);
}

void free_error(void *ptr){
    fprintf(stderr, "Tried freeing a UFO, but the provided address is not a UFO address. %lx\n", (uintptr_t) ptr);
    //TODO: die loudly?
}

void __ufo_free(R_allocator_t *allocator, void *ptr) {
    UfoObj object = ufo_get_by_address(&__ufo_system, ptr);
    if (ufo_is_error(&object)) {
        free_error(ptr);
        return;
    }   
    ufo_free(object);
    ufo_source_t* source = (ufo_source_t*) allocator->data;
    source->destructor_function(source->data);
    if (source->dimensions != NULL) {
        free(source->dimensions);
    }    
    free(source);
}

SEXPTYPE ufo_type_to_vector_type (ufo_vector_type_t ufo_type) {
    switch (ufo_type) {
        case UFO_CHAR: return CHARSXP;
        case UFO_LGL:  return LGLSXP;
        case UFO_INT:  return INTSXP;
        case UFO_REAL: return REALSXP;
        case UFO_CPLX: return CPLXSXP;
        case UFO_RAW:  return RAWSXP;
        case UFO_STR:  return STRSXP;
        default:       Rf_error("Cannot convert ufo_type_t=%i to SEXPTYPE", ufo_type);
                       return -1;
    }
}

ufo_vector_type_t vector_type_to_ufo_type (SEXPTYPE sexp_type) {
    switch (sexp_type) {
        case CHARSXP: return UFO_CHAR;
        case LGLSXP:  return UFO_LGL;
        case INTSXP:  return UFO_INT;
        case REALSXP: return UFO_REAL;
        case CPLXSXP: return UFO_CPLX;
        case RAWSXP:  return UFO_RAW;
        case STRSXP:  return UFO_STR;
        default:       Rf_error("Cannot convert SEXPTYPE=%i to ufo_vector_type_t", sexp_type);
                       return -1;
    }
}

uint32_t element_width_from_type_or_die(SEXPTYPE type) {
    switch(type) {
        case CHARSXP: return strideOf(Rbyte);
        case LGLSXP:  return strideOf(Rboolean);
        case INTSXP:  return strideOf(int);
        case REALSXP: return strideOf(double);
        case CPLXSXP: return strideOf(Rcomplex);
        case RAWSXP:  return strideOf(Rbyte);
        case STRSXP:  return strideOf(SEXP);
        default:      Rf_error("Cannot calculate element width for vector type: %d\n", type);
    }
}

R_allocator_t* __ufo_new_allocator(ufo_source_t* source) {
    // Initialize an allocator.
    R_allocator_t* allocator = (R_allocator_t*) malloc(sizeof(R_allocator_t));

    // Initialize an allocator data struct.
    //ufo_source_t* data = (ufo_source_t*) malloc(sizeof(ufo_source_t));

    // Configure the allocator: provide function to allocate and free memory,
    // as well as a structure to keep the allocator's data.
    allocator->mem_alloc = &__ufo_alloc;
    allocator->mem_free = &__ufo_free;
    allocator->res = NULL; /* reserved, must be NULL */
    allocator->data = source; /* custom data: used for source */

    return allocator;
}

int __vector_will_be_scalarized(SEXPTYPE type, size_t length) {
    return length == 1 && (type == REALSXP || type == INTSXP || type == LGLSXP);
}

void __prepopulate_scalar(SEXP scalar, ufo_source_t* source) {
    source->population_function(source->data, 0, source->vector_size, DATAPTR(scalar));
}

void __reset_vector(SEXP vector) {
	 UfoObj object = ufo_get_by_address(&__ufo_system, vector);
	 if (ufo_is_error(&object)) {
	     Rf_error("Tried resetting a UFO, "
	              "but the provided address is not a UFO header address.");
	 }

	 ufo_reset(&object);
}

SEXP ufo_alloc(SEXPTYPE type, R_xlen_t size, R_allocator_t* allocator) {
    switch (type) {
        // Using bad strings to create out-of-core character vectors.
        case CHARSXP: // TODO require a compile flag for bad strings
        Rf_warning("Warning, UFO is producing an out of core character vector.");
        return allocBadCharacter3(size, allocator);

        // The typical case
        default:
        return allocVector3(type, size, allocator);
    }
}

SEXP ufo_new(ufo_source_t* source) {
    // Check type.
    SEXPTYPE type = ufo_type_to_vector_type(source->vector_type);
    if (type < 0) {
        Rf_error("No available vector constructor for this type.");
    }

    // Initialize an allocator.
    R_allocator_t* allocator = __ufo_new_allocator(source);

    // Create a new vector of the appropriate type using the allocator.
    SEXP ufo = PROTECT(ufo_alloc(type, source->vector_size, allocator));

    // Workaround for scalar vectors ignoring custom allocator:
    // Pre-load the data in, at least it'll work as read-only.
    if (__vector_will_be_scalarized(type, source->vector_size)) {
        __prepopulate_scalar(ufo, source);
    }

    // Workaround for strings being populated with empty string pointers in vectorAlloc3.
    // Reset will remove all values from the vector and get rid of the temporary files on disk.
    if (type == STRSXP && ufo_address_is_ufo_object(&__ufo_system, ufo)) {
    	__reset_vector(ufo);
    }

    //printf("UFO=%p\n", (void *) result);

    UNPROTECT(1);
    return ufo;
}

SEXP ufo_new_multidim(ufo_source_t* source) {
    // Check type.
    SEXPTYPE type = ufo_type_to_vector_type(source->vector_type);
    if (type < 0) {
        Rf_error("No available vector constructor for this type.");
    }

    // Initialize an allocator.
    R_allocator_t* allocator = __ufo_new_allocator(source);

    // Create a new matrix of the appropriate type using the allocator.
    SEXP ufo = PROTECT(allocMatrix3(type, source->dimensions[0],
                                    source->dimensions[1], allocator));

    // Workaround for scalar vectors ignoring custom allocator:
    // Pre-load the data in, at least it'll work as read-only.
    if (__vector_will_be_scalarized(type, source->vector_size)) {
        __prepopulate_scalar(ufo, source);
    }

    // Workaround for strings being populated with empty string pointers in vectorAlloc3.
    // Reset will remove all values from the vector and get rid of the temporary files on disk.
    if (type == STRSXP) {
    	__reset_vector(ufo);
    }

    UNPROTECT(1);
    return ufo;
}

SEXP is_ufo(SEXP x) {
	SEXP/*LGLSXP*/ response = PROTECT(allocVector(LGLSXP, 1));
	if(ufo_address_is_ufo_object(&__ufo_system, x)) {
		SET_LOGICAL_ELT(response, 0, 1);//TODO Rtrue and Rfalse
	} else {
		SET_LOGICAL_ELT(response, 0, 0);
	}
	UNPROTECT(1);
	return response;
}

typedef struct {
    u_int64_t population_id;
    u_int64_t writeback_id;
    u_int64_t finalizer_id;
    u_int64_t user_data_id;
} sandbox_data_t;

int32_t populate_in_sandbox(void *user_data, uintptr_t start, 
                            uintptr_t end, unsigned char *target) {

    sandbox_data_t *info = (sandbox_data_t *) user_data;

    // Call remote
    //info->population_id
    //>> populate(u_int64_t function_id, u_int_t user_data_id);

    // Get result and plug into memory
}
 
void writeback_in_sandbox(void *user_data, UfoWriteListenerEvent event) {

    sandbox_data_t *info = (sandbox_data_t *) user_data;

    // Call remote    
    //info->writeback_id
}

void free_in_sandbox(void *user_data) {
    // Call remote
    //info->finalizer_id
    //info->user_data_id

    // Clean up locally
    free(user_data);
}

SEXP/*<T: VECSXP>*/ ufo_new_sandbox(            // R-callable wrapper from ufo_new, wraps function for sandboxed execution
    SEXP/*FUNSXP*/         population_sexp,     // (..user_data) -> <T: VECSXP>
    SEXP/*FUNSXP|NILSXP*/  writeback_sexp,      // (memory: <T: VECSXP>, ..user_data) -> NILSXP
    SEXP/*FUNSXP|NILSXP*/  finalizer_sexp,      // (..user_data) -> NILSXP
    SEXP/*<T: VECSXP>*/    prototype_sexp,      // length: 0 (not enforced) which indicates the type the resulting UFO is
    SEXP/*INTSXP|REALSXP*/ length_sexp,         // length: 1, converted to R_xlen_t
    SEXP/*LISTSXP*/        user_data_sexp,      // passed to population, writeback, finalizer via ...
    SEXP/*INTSXP*/         min_load_count_sexp, // length: 1
    SEXP/*LGLSXP*/         read_only_sexp,      // length: 1
) {
    // Determine UFO vector type.
    SEXPTYPE sexp_type = TYPEOF(prototype_sexp);
    switch (vector_type != CHARSXP && vector_type != LGLSXP 
         && vector_type != INTSXP  && vector_type != REALSXP 
         && vector_type != CPLXSXP && vector_type != RAWSXP 
         && vector_type != STRSXP) {
        Rf_error("Cannot create UFO vector of type ", type2char(vector_type));
    }
    ufo_vector_type_t vector_type = vector_type_to_ufo_type(sexp_type);

    // Determine sizes
    size_t element_size = __get_element_size(vector_type);
    size_t vector_size = __extract_R_xlen_t_or_die(length_sexp);

    // Auxiliaries
    int min_load_count = __extract_int_or_die(min_load_count_sexp);
    bool read_only = __extract_boolean_or_die(read_only_sexp);

    // TODO
    // Start and init the remote if not started
    // TODO: also remember to shutdown later

    // Register functions and data with sandbox
    u_int64_t population_id = 0; // TODO 
    u_int64_t writeback_id = 0;  // TODO 
    u_int64_t finalizer_id = 0;  // TODO 
    u_int64_t user_data_id = 0;  // TODO 

    // Allocate and populate the structs
    sandbox_data_t *info = (sandbox_data_t *) malloc(sizeof(sandbox_data_t));
    info->population_id = population_id;
    info->writeback_id = writeback_id;
    info->finalizer_id = finalizer_id;
    info->user_data_id = user_data_id;

    ufo_source_t* source = (ufo_source_t*) malloc(sizeof(ufo_source_t));
    source->data = info;
    source->population_function = populate_in_sandbox;
    source->writeback_function = writeback_in_sandbox;
    source->destructor_function = free_in_sandbox;        
    source->vector_type = vector_type;
    source->vector_size = vector_size;
    source->element_size = element_size;
    source->dimensions = NULL;
    source->dimensions_length = 0;
    source->min_load_count = min_load_count;
    source->read_only = read_only;  

    // Create UFO and done
    return ufo_new(source);
}

