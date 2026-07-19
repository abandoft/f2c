#include "core/generated/private.h"

static void emit_record_status(Buffer *output) {
    f2c_buffer_append(
        output,
        "enum { F2C_IO_STATUS_OK = 1, F2C_IO_STATUS_UNIT = 2, F2C_IO_STATUS_ACTION = 3, "
        "F2C_IO_STATUS_FORM = 4, F2C_IO_STATUS_ACCESS = 5, F2C_IO_STATUS_RECORD = 6, "
        "F2C_IO_STATUS_CORRUPT = 7, F2C_IO_STATUS_OVERFLOW = 8 };\n"
        "static inline F2C_UNUSED bool f2c_io_is_error(int status) { return status != "
        "F2C_IO_STATUS_OK && status != EOF && status != -2; }\n"
        "static inline F2C_UNUSED int32_t f2c_io_status_value(int status) { return status == "
        "F2C_IO_STATUS_OK ? 0 : status == EOF ? -1 : status == -2 ? -2 : status > 1 ? "
        "(int32_t)status : 1; }\n"
        "static inline F2C_UNUSED void f2c_io_abort_unhandled(int status, const char *operation) "
        "{ if (status == F2C_IO_STATUS_OK) return; fprintf(stderr, \"Fortran %s failed: %s\\n\", "
        "operation, status == EOF ? \"end of file\" : status == -2 ? \"end of record\" : "
        "status == F2C_IO_STATUS_UNIT ? \"invalid unit\" : status == F2C_IO_STATUS_ACTION ? "
        "\"unit action mismatch\" : status == F2C_IO_STATUS_FORM ? \"formatted/unformatted "
        "connection mismatch\" : status == F2C_IO_STATUS_ACCESS ? \"sequential/direct access "
        "mismatch\" : status == F2C_IO_STATUS_RECORD ? \"record transfer failed\" : status == "
        "F2C_IO_STATUS_CORRUPT ? \"corrupt unformatted record\" : status == "
        "F2C_IO_STATUS_OVERFLOW ? \"record offset overflow\" : \"I/O error\"); abort(); }\n"
        "static inline F2C_UNUSED bool f2c_binary_read(f2c_io_stream *stream, void *value, "
        "size_t size) { return size == 0U || f2c_stream_read(value, size, stream) == size; }\n"
        "static inline F2C_UNUSED bool f2c_binary_write(f2c_io_stream *stream, const void "
        "*value, size_t size) { return size == 0U || f2c_stream_write(value, size, stream) == "
        "size; }\n");
}

static void emit_record_encoding(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED bool f2c_record_write_u64(FILE *file, uint64_t value) { "
        "unsigned char bytes[8]; size_t index; for (index = 0U; index < sizeof(bytes); ++index) "
        "{ bytes[index] = (unsigned char)(value & UINT64_C(255)); value >>= 8U; } return file "
        "!= NULL && fwrite(bytes, 1U, sizeof(bytes), file) == sizeof(bytes); }\n"
        "static inline F2C_UNUSED int f2c_record_read_u64(FILE *file, uint64_t *value) { "
        "unsigned char bytes[8]; size_t count, index; uint64_t result = 0U; if (file == NULL || "
        "value == NULL) return -1; count = fread(bytes, 1U, sizeof(bytes), file); if (count == "
        "0U && feof(file)) return 0; if (count != sizeof(bytes)) return -1; for (index = "
        "sizeof(bytes); index != 0U; --index) result = (result << 8U) | bytes[index - 1U]; "
        "*value = result; return 1; }\n"
        "static inline F2C_UNUSED bool f2c_record_file_size(FILE *file, long *size) { long "
        "position, end; if (file == NULL || size == NULL || (position = ftell(file)) < 0L || "
        "fseek(file, 0L, SEEK_END) != 0 || (end = ftell(file)) < 0L || fseek(file, position, "
        "SEEK_SET) != 0) return false; *size = end; return true; }\n");
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED bool f2c_backspace_unformatted(f2c_unit_entry *entry) { "
        "long position, marker_position, start; uint64_t trailer = 0U, header = 0U; if (entry "
        "== NULL || entry->file == NULL || entry->access != F2C_ACCESS_SEQUENTIAL || "
        "entry->form != F2C_FORM_UNFORMATTED) return false; clearerr(entry->file); position = "
        "ftell(entry->file); if (position == 0L) return true; if (position < 16L) return false; "
        "marker_position = position - 8L; if (fseek(entry->file, marker_position, SEEK_SET) != "
        "0 || f2c_record_read_u64(entry->file, &trailer) != 1 || trailer > "
        "(uint64_t)LONG_MAX || marker_position < 8L || trailer > "
        "(uint64_t)(marker_position - 8L)) return false; start = marker_position - "
        "(long)trailer - 8L; if (fseek(entry->file, start, SEEK_SET) != 0 || "
        "f2c_record_read_u64(entry->file, &header) != 1 || header != trailer) return false; "
        "clearerr(entry->file); return fseek(entry->file, start, SEEK_SET) == 0; }\n");
}

static void emit_transfer_model(Buffer *output) {
    f2c_buffer_append(
        output,
        "typedef struct f2c_io_transfer { f2c_unit_entry *unit; f2c_io_stream stream_storage; "
        "f2c_io_stream *stream; long header_position; long payload_position; uint64_t "
        "payload_length; bool input; bool formatted; bool direct; bool sequential_unformatted; "
        "bool child; bool active; } f2c_io_transfer;\n"
        "static inline F2C_UNUSED int f2c_transfer_direct_begin(f2c_io_transfer *transfer, "
        "int64_t record) { uint64_t ordinal, offset, length; long file_size; f2c_unit_entry "
        "*entry = transfer->unit; if (record <= 0 || entry->recl <= 0 || entry->file == NULL) "
        "return F2C_IO_STATUS_RECORD; ordinal = (uint64_t)record - UINT64_C(1); length = "
        "(uint64_t)(uint32_t)entry->recl; if (ordinal > UINT64_MAX / length) return "
        "F2C_IO_STATUS_OVERFLOW; offset = ordinal * length; if (offset > (uint64_t)LONG_MAX || "
        "length > (uint64_t)LONG_MAX - offset) return F2C_IO_STATUS_OVERFLOW; if "
        "(transfer->input && (!f2c_record_file_size(entry->file, &file_size) || "
        "(uint64_t)file_size < offset + length)) return F2C_IO_STATUS_RECORD; if "
        "(!f2c_stream_initialize_external_record(&transfer->stream_storage, entry->file, "
        "(long)offset, (size_t)length, transfer->formatted, transfer->input, entry->pad)) "
        "return F2C_IO_STATUS_RECORD; if (transfer->formatted && "
        "!f2c_stream_enable_record_sequence(&transfer->stream_storage, transfer->input ? "
        "(size_t)file_size : (size_t)LONG_MAX)) return F2C_IO_STATUS_RECORD; transfer->stream "
        "= &transfer->stream_storage; "
        "transfer->direct = true; return F2C_IO_STATUS_OK; }\n");
}

static void emit_transfer_begin(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED int f2c_transfer_sequential_unformatted_begin("
        "f2c_io_transfer *transfer) { f2c_unit_entry *entry = transfer->unit; int header; "
        "uint64_t length = 0U; if (entry->file == NULL) return F2C_IO_STATUS_UNIT; if "
        "(transfer->input) { header = f2c_record_read_u64(entry->file, &length); if (header == "
        "0) return EOF; if (header < 0 || length > (uint64_t)LONG_MAX) return "
        "F2C_IO_STATUS_CORRUPT; transfer->payload_position = ftell(entry->file); if "
        "(transfer->payload_position < 0L || (uint64_t)transfer->payload_position > "
        "(uint64_t)LONG_MAX - length || !f2c_stream_initialize_external_record("
        "&transfer->stream_storage, entry->file, transfer->payload_position, (size_t)length, "
        "false, true, false)) return F2C_IO_STATUS_CORRUPT; transfer->payload_length = length; "
        "} else { transfer->header_position = ftell(entry->file); if "
        "(transfer->header_position < 0L || !f2c_record_write_u64(entry->file, 0U)) return "
        "F2C_IO_STATUS_RECORD; transfer->payload_position = ftell(entry->file); if "
        "(transfer->payload_position < 0L) return F2C_IO_STATUS_RECORD; "
        "f2c_stream_initialize_external(&transfer->stream_storage, entry->file); } "
        "transfer->stream = &transfer->stream_storage; transfer->sequential_unformatted = "
        "true; return F2C_IO_STATUS_OK; }\n"
        "static inline F2C_UNUSED int f2c_transfer_begin(f2c_io_transfer *transfer, int32_t "
        "unit, bool input, bool formatted, bool has_record, int64_t record) { f2c_unit_entry "
        "*entry; f2c_io_stream *stream; int status = F2C_IO_STATUS_OK; memset(transfer, 0, "
        "sizeof(*transfer)); transfer->input = input; transfer->formatted = formatted; entry = "
        "f2c_find_unit(unit); if (entry != NULL && f2c_child_io_depth != 0U && "
        "entry->active_stream != NULL) { if (entry->form != (formatted ? F2C_FORM_FORMATTED : "
        "F2C_FORM_UNFORMATTED)) return F2C_IO_STATUS_FORM; transfer->unit = entry; "
        "transfer->stream = entry->active_stream; transfer->child = true; transfer->active = "
        "true; return F2C_IO_STATUS_OK; } if (entry == NULL) { stream = "
        "f2c_unit_stream(unit, input); if (stream == NULL) return F2C_IO_STATUS_ACTION; entry = "
        "f2c_find_unit(unit); if (entry == NULL) { if (!formatted) return "
        "F2C_IO_STATUS_FORM; if (has_record) return F2C_IO_STATUS_ACCESS; transfer->stream = "
        "stream; transfer->active = true; return F2C_IO_STATUS_OK; } } transfer->unit = entry; "
        "if ((input && entry->action == F2C_ACTION_WRITE) || (!input && entry->action == "
        "F2C_ACTION_READ)) return F2C_IO_STATUS_ACTION; if (entry->form != (formatted ? "
        "F2C_FORM_FORMATTED : F2C_FORM_UNFORMATTED)) return F2C_IO_STATUS_FORM; if "
        "(entry->internal) { if (!formatted || has_record) return !formatted ? "
        "F2C_IO_STATUS_FORM : F2C_IO_STATUS_ACCESS; transfer->stream = entry->stream; "
        "transfer->child = f2c_child_io_depth != 0U; } else if (entry->access == "
        "F2C_ACCESS_DIRECT) { if (!has_record) return F2C_IO_STATUS_ACCESS; status = "
        "f2c_transfer_direct_begin(transfer, record); } else { if (has_record) return "
        "F2C_IO_STATUS_ACCESS; if (formatted) transfer->stream = entry->stream; else status = "
        "f2c_transfer_sequential_unformatted_begin(transfer); } if (status != "
        "F2C_IO_STATUS_OK) return status; transfer->active = true; if (!transfer->child) "
        "entry->active_stream = transfer->stream; return F2C_IO_STATUS_OK; }\n");
}

static void emit_transfer_end(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED int f2c_transfer_fill(f2c_io_transfer *transfer, unsigned "
        "char value) { unsigned char block[128]; memset(block, value, sizeof(block)); while "
        "(transfer->stream_storage.position < transfer->stream_storage.virtual_size) { size_t "
        "remaining = transfer->stream_storage.virtual_size - "
        "transfer->stream_storage.position; size_t count = remaining < sizeof(block) ? "
        "remaining : sizeof(block); if (f2c_stream_write(block, count, transfer->stream) != "
        "count) return F2C_IO_STATUS_RECORD; } return F2C_IO_STATUS_OK; }\n"
        "static inline F2C_UNUSED int f2c_transfer_end(f2c_io_transfer *transfer, int status) "
        "{ f2c_unit_entry *entry; FILE *file; uint64_t trailer = 0U; long end, after; int "
        "read_status; if (transfer == NULL || !transfer->active) return status; entry = "
        "transfer->unit; if (transfer->child) return status; if (entry != NULL && "
        "entry->active_stream == transfer->stream) entry->active_stream = NULL; if (entry == "
        "NULL) return status; file = entry->file; if (transfer->direct) { if (!transfer->input "
        "&& status == F2C_IO_STATUS_OK) status = f2c_transfer_fill(transfer, "
        "transfer->formatted ? (unsigned char)' ' : (unsigned char)0); if "
        "(f2c_stream_error(transfer->stream) && status == F2C_IO_STATUS_OK) status = "
        "F2C_IO_STATUS_RECORD; if (file == NULL || fseek(file, (long)transfer->stream_storage."
        "virtual_size, SEEK_SET) != 0) status = F2C_IO_STATUS_RECORD; return status; } if "
        "(!transfer->sequential_unformatted) { if (f2c_stream_error(transfer->stream) && "
        "status == F2C_IO_STATUS_OK) status = F2C_IO_STATUS_RECORD; return status; } if "
        "(transfer->input) { if (transfer->payload_length > (uint64_t)LONG_MAX || "
        "transfer->payload_position > LONG_MAX - (long)transfer->payload_length || fseek(file, "
        "transfer->payload_position + (long)transfer->payload_length, SEEK_SET) != 0) return "
        "F2C_IO_STATUS_CORRUPT; read_status = f2c_record_read_u64(file, &trailer); if "
        "(read_status != 1 || trailer != transfer->payload_length) return "
        "F2C_IO_STATUS_CORRUPT; return status; } end = ftell(file); if (end < "
        "transfer->payload_position) return F2C_IO_STATUS_RECORD; transfer->payload_length = "
        "(uint64_t)(end - transfer->payload_position); if (!f2c_record_write_u64(file, "
        "transfer->payload_length)) return F2C_IO_STATUS_RECORD; after = ftell(file); if "
        "(after < 0L || fseek(file, transfer->header_position, SEEK_SET) != 0 || "
        "!f2c_record_write_u64(file, transfer->payload_length) || fseek(file, after, SEEK_SET) "
        "!= 0) return F2C_IO_STATUS_RECORD; return status; }\n");
}

void f2c_emit_record_io_support(Buffer *output) {
    emit_record_status(output);
    emit_record_encoding(output);
    emit_transfer_model(output);
    emit_transfer_begin(output);
    emit_transfer_end(output);
}
