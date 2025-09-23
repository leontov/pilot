
#include <stdlib.h>
#include <string.h>

#include "kolibri_decimal_cell.h"
#include "kolibri_ping.h"


    }
}

void cleanup_decimal_cell(decimal_cell_t* cell) {

    }
    return current;
}


        }

        update_cell_state(child);
    }
    return count;
}

size_t decimal_cell_serialize(const decimal_cell_t* cell, char* buffer, size_t buffer_size) {
    if (!cell) {
        if (buffer_size > 0) buffer[0] = '\0';
        return 0;
    }
    size_t total = serialize_node_internal(cell, buffer, buffer_size, 0);
    if (buffer_size > 0) {
        size_t term = (total < buffer_size - 1) ? total : (buffer_size - 1);
        buffer[term] = '\0';
    }
    return total;
}

