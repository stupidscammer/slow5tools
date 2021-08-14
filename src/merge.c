/**
 * @file merge.c
 * @brief merge two or more SLOW5 files
 * @author Hiruna Samarakoon (h.samarakoon@garvan.org.au)
 * @date 27/02/2021
 */
#include <getopt.h>
#include <sys/wait.h>

#include <string>
#include <vector>
#include <pthread.h>

#include "error.h"
#include "cmd.h"
#include <slow5/slow5.h>
#include "read_fast5.h"
#include "slow5_extra.h"
#include "misc.h"
#include "thread.h"

#define DEFAULT_NUM_THREADS (4)
#define READ_ID_BATCH_CAPACITY (4096)
#define USAGE_MSG "Usage: %s [OPTION]... [SLOW5_FILE/DIR]...\n"
#define HELP_SMALL_MSG "Try '%s --help' for more information.\n"
#define HELP_LARGE_MSG \
    "Merge multiple SLOW5/BLOW5 files to a single file\n" \
    USAGE_MSG \
    "\n" \
    "OPTIONS:\n" \
    "    --to [STR]                         output in the format specified in STR. slow5 for SLOW5 ASCII. blow5 for SLOW5 binary (BLOW5) [default: BLOW5]\n" \
    "    -c, --compress [compression_type]  convert to compressed blow5 [default: zlib]\n" \
    "    -o, --output [FILE]                output contents to FILE [default: stdout]\n" \
    "    ---tmp-prefix [STR]                path to crete a directory to write temporary files\n"                   \
    "    -l, --lossless [STR]               retain information in auxilliary fields during the conversion.[default: true].\n" \
    "    -t, --threads [INT]                number of threads [default: 4]\n"        \
    "    -h, --help                         display this message and exit\n" \
    "    --parallel-files [STR]             divide files among threads as opposed to using a dividing a batch of reads\n"                                    \
    "    -K --batchsize                     the number of records on the memory at once. [default: 4096]\n" \

static double init_realtime = 0;

int delete_directory(std::string output_dir);

void parallel_reads_model(core_t *core, db_t *db, int32_t i) {
    //
    struct slow5_rec *read = NULL;
    if (slow5_rec_depress_parse(&db->mem_records[i], &db->mem_bytes[i], NULL, &read, core->fp) != 0) {
        exit(EXIT_FAILURE);
    } else {
        free(db->mem_records[i]);
    }
    read->read_group = db->list[core->slow5_file_index][read->read_group]; //write records of the ith slow5file with the updated read_group value

    struct slow5_press *press_ptr = slow5_press_init(core->press_method);
    size_t len;
    if ((db->read_record[i].buffer = slow5_rec_to_mem(read, core->fp->header->aux_meta, core->format_out, press_ptr, &len)) == NULL) {
        slow5_press_free(press_ptr);
        slow5_rec_free(read);
        exit(EXIT_FAILURE);
    }
    slow5_press_free(press_ptr);
    db->read_record[i].len = len;
    slow5_rec_free(read);
}


void parallel_files_model(core_t *core, db_t *db, int32_t i){
    std::string out = db->output_dir;
    out += "/" + std::to_string(i) + ".blow5";
    const char* output_path = out.c_str();
    FILE* slow5_file_pointer = fopen(output_path, "a");
    if (!slow5_file_pointer) {
        ERROR("Output file %s could not be opened - %s.", output_path, strerror(errno));
        exit(EXIT_FAILURE);
    }
    slow5_file_t* slow5File = slow5_init_empty(slow5_file_pointer, output_path, core->format_out);
    slow5_hdr_initialize(slow5File->header, core->lossy);
    slow5_file_t *slow5File_i = slow5_open(db->slow5_files[i].c_str(), "r");
    if (!slow5File_i) {
        ERROR("cannot open %s. skipping...\n", db->slow5_files[i].c_str());
        exit(EXIT_FAILURE);
    }
    struct slow5_rec *read = NULL;
    struct slow5_press* compress = slow5_press_init(core->press_method);
    int ret;
    while ((ret = slow5_get_next(&read, slow5File_i)) >= 0) {
        read->read_group = db->list[i][read->read_group]; //write records of the ith slow5file with the updated read_group value
        if (slow5_rec_fwrite(slow5File->fp, read, slow5File->header->aux_meta, core->format_out, compress) == -1) {
            slow5_rec_free(read);
            ERROR("Could not write records to temp file %s\n", output_path);
            exit(EXIT_FAILURE);
        }
    }
    slow5_rec_free(read);
    slow5_press_free(compress);
    slow5_close(slow5File_i);
    slow5_close(slow5File);
}

int merge_main(int argc, char **argv, struct program_meta *meta){
    init_realtime = slow5_realtime();

    // Debug: print arguments
    if (meta != NULL && meta->verbosity_level >= LOG_DEBUG) {
        if (meta->verbosity_level >= LOG_VERBOSE) {
            VERBOSE("printing the arguments given%s","");
        }

        fprintf(stderr, DEBUG_PREFIX "argv=[",
                __FILE__, __func__, __LINE__);
        for (int i = 0; i < argc; ++ i) {
            fprintf(stderr, "\"%s\"", argv[i]);
            if (i == argc - 1) {
                fprintf(stderr, "]");
            } else {
                fprintf(stderr, ", ");
            }
        }
        fprintf(stderr, NO_COLOUR);
    }

    // No arguments given
    if (argc <= 1) {
        fprintf(stderr, HELP_LARGE_MSG, argv[0]);
        EXIT_MSG(EXIT_FAILURE, argv, meta);
        return EXIT_FAILURE;
    }

    static struct option long_opts[] = {
            {"help", no_argument, NULL, 'h'},  //0
            {"threads", required_argument, NULL, 't' }, //1
            {"to", required_argument, NULL, 'b'},    //2
            {"compress", required_argument, NULL, 'c'},  //3
            { "lossless", required_argument, NULL, 'l'}, //4
            {"output", required_argument, NULL, 'o'}, //5
            {"tmp-prefix", required_argument, NULL, 'f'}, //6
            {"parallel-files", required_argument, NULL, 'p'}, //7
            {"batchsize", required_argument, NULL, 'K'}, //8

            {NULL, 0, NULL, 0 }
    };

    // Default options
    enum slow5_fmt format_out = SLOW5_FORMAT_BINARY;
    enum slow5_press_method pressMethod = SLOW5_COMPRESS_ZLIB;
    int compression_set = 0;

    // Input arguments
    char *arg_fname_out = NULL;
    char *arg_num_threads = NULL;
    char *arg_temp_dir = NULL;
    int lossy = 0;
    int parallel_files = 1;

    size_t num_threads = DEFAULT_NUM_THREADS;
    int64_t read_id_batch_capacity = READ_ID_BATCH_CAPACITY;

    int opt;
    int longindex = 0;

    // Parse options
    while ((opt = getopt_long(argc, argv, "b:c:hl:t:o:f:p:K:", long_opts, &longindex)) != -1) {
        if (meta->verbosity_level >= LOG_DEBUG) {
            DEBUG("opt='%c', optarg=\"%s\", optind=%d, opterr=%d, optopt='%c'",
                  opt, optarg, optind, opterr, optopt);
        }
        switch (opt) {
            case 'h':
                if (meta->verbosity_level >= LOG_VERBOSE) {
                    VERBOSE("displaying large help message%s","");
                }
                fprintf(stdout, HELP_LARGE_MSG, argv[0]);
                EXIT_MSG(EXIT_SUCCESS, argv, meta);
                exit(EXIT_SUCCESS);
            case 't':
                arg_num_threads = optarg;
                break;
            case 'b':
                if(strcmp(optarg,"slow5")==0){
                    format_out = SLOW5_FORMAT_ASCII;
                }else if(strcmp(optarg,"blow5")==0){
                    format_out = SLOW5_FORMAT_BINARY;
                }else{
                    ERROR("Incorrect output format%s", "");
                    return EXIT_FAILURE;
                }
                break;
            case 'K':
                read_id_batch_capacity = atoi(optarg);
                if(read_id_batch_capacity < 0){
                    fprintf(stderr, "batchsize cannot be negative\n");
                    fprintf(stderr, HELP_SMALL_MSG, argv[0]);
                    EXIT_MSG(EXIT_FAILURE, argv, meta);
                    return EXIT_FAILURE;
                }
                break;
            case 'c':
                compression_set = 1;
                if(strcmp(optarg,"none")==0){
                    pressMethod = SLOW5_COMPRESS_NONE;
                }else if(strcmp(optarg,"zlib")==0){
                    pressMethod = SLOW5_COMPRESS_ZLIB;
                }else{
                    ERROR("Incorrect compression type%s", "");
                    return EXIT_FAILURE;
                }
                break;
            case 'l':
                if(strcmp(optarg,"true")==0){
                    lossy = 0;
                }else if(strcmp(optarg,"false")==0){
                    lossy = 1;
                }else{
                    ERROR("Incorrect argument%s", "");
                    return EXIT_FAILURE;
                }
                break;
            case 'p':
                if(strcmp(optarg,"true")==0){
                    parallel_files = 1;
                }else if(strcmp(optarg,"false")==0){
                    parallel_files = 0;
                }else{
                    ERROR("Incorrect argument%s", "");
                    return EXIT_FAILURE;
                }
                break;
            case 'o':
                arg_fname_out = optarg;
                break;
            case 'f':
                arg_temp_dir = optarg;
                break;
            default: // case '?'
                fprintf(stderr, HELP_SMALL_MSG, argv[0]);
                EXIT_MSG(EXIT_FAILURE, argv, meta);
                return EXIT_FAILURE;
        }
    }
    if(compression_set == 0 && format_out == SLOW5_FORMAT_ASCII){
        pressMethod = SLOW5_COMPRESS_NONE;
    }
    // compression option is only effective with -b blow5
    if(compression_set == 1 && format_out == SLOW5_FORMAT_ASCII){
        ERROR("%s","Compression option (-c) is only available for SLOW5 binary format.");
        return EXIT_FAILURE;
    }

    // Parse num threads argument
    if (arg_num_threads != NULL) {
        char *endptr;
        long ret = strtol(arg_num_threads, &endptr, 10);

        if (*endptr == '\0') {
            num_threads = ret;
        } else {
            MESSAGE(stderr, "invalid number of threads -- '%s'", arg_num_threads);
            fprintf(stderr, HELP_SMALL_MSG, argv[0]);

            EXIT_MSG(EXIT_FAILURE, argv, meta);
            return EXIT_FAILURE;
        }
    }

    // Check for remaining files to parse
    if (optind >= argc) {
        MESSAGE(stderr, "missing slow5 files or directories%s", "");
        fprintf(stderr, HELP_SMALL_MSG, argv[0]);
        EXIT_MSG(EXIT_FAILURE, argv, meta);
        return EXIT_FAILURE;
    }

    std::string output_file;
    std::string extension;
    if(arg_fname_out){
        output_file = std::string(arg_fname_out);
        extension = output_file.substr(output_file.length()-6, output_file.length());
    }

    if(arg_fname_out && format_out==SLOW5_FORMAT_ASCII && extension!=".slow5"){
        ERROR("Output file extension '%s' does not match with the output format:FORMAT_ASCII", extension.c_str());
        fprintf(stderr, HELP_SMALL_MSG, argv[0]);
        EXIT_MSG(EXIT_FAILURE, argv, meta);
        return EXIT_FAILURE;
    }else if(arg_fname_out && format_out==SLOW5_FORMAT_BINARY && extension!=".blow5"){
        ERROR("Output file extension '%s' does not match with the output format:FORMAT_BINARY", extension.c_str());
        fprintf(stderr, HELP_SMALL_MSG, argv[0]);
        EXIT_MSG(EXIT_FAILURE, argv, meta);
        return EXIT_FAILURE;
    }

    char buff[20];
    time_t now = time(NULL);
    strftime(buff, 20, "%H%M%S", localtime(&now));
    std::string tstamp = buff;
    int p_id=getpid();  /*process id*/
    std::string output_dir = "slow5_"+tstamp+"_"+std::to_string(p_id);

    if(arg_temp_dir){
        output_dir = std::string(arg_temp_dir);
    }
    //    fprintf(stderr, "output_file=%s output_dir=%s\n",output_file.c_str(),output_dir.c_str());
    //create tmp-prefix directory
    struct stat st = {0};
    if (stat(output_dir.c_str(), &st) == -1) {
        mkdir(output_dir.c_str(), 0700);
    }else{
        std::vector< std::string > dir_list = list_directory(output_dir.c_str());
        if(dir_list.size()>2){
            ERROR("Temp-prefix director %s is not empty",output_dir.c_str());
            return EXIT_FAILURE;
        }
    }

    //measure file listing time
    double realtime0 = slow5_realtime();
    std::vector<std::string> files;
    for (int i = optind; i < argc; ++i) {
        list_all_items(argv[i], files, 0, NULL);
    }
    fprintf(stderr, "[%s] %ld files found - took %.3fs\n", __func__, files.size(), slow5_realtime() - realtime0);


    //determine new read group numbers
    //measure read_group number allocation time
    realtime0 = slow5_realtime();

    FILE* slow5_file_pointer = stdout;
    if(arg_fname_out){
        slow5_file_pointer = fopen(arg_fname_out, "wb");
        if (!slow5_file_pointer) {
            ERROR("Output file %s could not be opened - %s.", arg_fname_out, strerror(errno));
            return EXIT_FAILURE;
        }
    }else{
        std::string stdout_s = "stdout";
        arg_fname_out = &stdout_s[0];
    }

    slow5_file_t* slow5File = slow5_init_empty(slow5_file_pointer, arg_fname_out, format_out);
    slow5_hdr_initialize(slow5File->header, lossy);
    slow5File->header->num_read_groups = 0;
    std::vector<std::vector<size_t>> list;
    size_t index = 0;
    std::vector<std::string> slow5_files;
    size_t num_files = files.size();
    for(size_t i=0; i<num_files; i++) { //iterate over slow5files
        slow5_file_t* slow5File_i = slow5_open(files[i].c_str(), "r");
        if(!slow5File_i){
            ERROR("[Skip file]: cannot open %s. skipping...\n",files[i].c_str());
            continue;
        }
        if(lossy==0 && slow5File_i->header->aux_meta == NULL){
            ERROR("[Skip file]: %s has no auxiliary fields. Specify -l false to merge files with no auxiliary fields.", files[i].c_str());
            slow5_close(slow5File_i);
            delete_directory(output_dir);
            return EXIT_FAILURE;
        }

        int64_t read_group_count_i = slow5File_i->header->num_read_groups; // number of read_groups in ith slow5file
        std::vector<size_t> read_group_tracker(read_group_count_i); //this array will store the new group_numbers of the ith slow5File, i.e., the new value of jth read_group_number
        list.push_back(read_group_tracker);

        for(int64_t j=0; j<read_group_count_i; j++){
            char* run_id_j = slow5_hdr_get("run_id", j, slow5File_i->header); // run_id of the jth read_group of the ith slow5file
            int64_t read_group_count = slow5File->header->num_read_groups; //since this might change during iterating; cannot know beforehand
            size_t flag_run_id_found = 0;
            for(int64_t k=0; k<read_group_count; k++){
                char* run_id_k = slow5_hdr_get("run_id", k, slow5File->header);
                if(strcmp(run_id_j,run_id_k) == 0){
                    flag_run_id_found = 1;
                    list[index][j] = k; //assumption0: if run_ids are similar the rest of the header attribute values of jth and kth read_groups are similar.
                    break;
                }
            }
            if(flag_run_id_found == 0){ // time to add a new read_group
                khash_t(slow5_s2s) *rg = slow5_hdr_get_data(j, slow5File_i->header); // extract jth read_group related data from ith slow5file
                int64_t new_read_group = slow5_hdr_add_rg_data(slow5File->header, rg); //assumption0
                if(new_read_group != read_group_count){ //sanity check
                    WARNING("New read group number is not equal to number of groups; something's wrong\n%s", "");
                }
                list[index][j] = new_read_group;
            }
        }
        slow5_close(slow5File_i);
        index++;
        slow5_files.push_back(files[i]);

    }

    if(slow5_files.size()==0){
        WARNING("No proper slow5/blow5 files found. Exiting...%s","");
        delete_directory(output_dir);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "[%s] Allocating new read group numbers - took %.3fs\n", __func__, slow5_realtime() - realtime0);

    //now write the header to the slow5File. Use Binary non compress method for fast writing
    if(slow5_hdr_fwrite(slow5File->fp, slow5File->header, format_out, pressMethod) == -1){
        ERROR("Could not write the header to %s\n", arg_fname_out);
        return EXIT_FAILURE;
    }

    if(parallel_files) {
        INFO("%s", "Using parallel files");
        size_t num_slow5s = slow5_files.size();
        if(num_threads >= num_slow5s){
            num_threads = num_slow5s;
        }

        // Setup multithreading structures
        core_t core;
        core.num_thread = num_threads;
        core.format_out = format_out;
        core.press_method = pressMethod;
        core.lossy = lossy;

        db_t db = {0};
        db.n_batch = slow5_files.size();
        db.slow5_files = slow5_files;
        db.list = list;
        db.output_dir = output_dir;

        //measure read_group number assigning using multi-threads time
        realtime0 = slow5_realtime();

        work_db(&core, &db, parallel_files_model);
        fprintf(stderr, "[%s] Assigning new read group numbers using %ld threads - took %.3fs\n", __func__, num_threads,
                slow5_realtime() - realtime0);

        slow5_files.clear();
        list_all_items(output_dir, slow5_files, 0, NULL);

        //measure single thread file concatenation time
        realtime0 = slow5_realtime();

        for (size_t i = 0; i < slow5_files.size(); i++) {

            // BUFSIZE default is 8192 bytes
            // BUFSIZE of 1 means one chareter at time
            // good values should fit to blocksize, like 1024 or 4096
            // higher values reduce number of system calls

            char buf[BUFSIZ];
            size_t size;

            FILE *slow5File_i = fopen(slow5_files[i].c_str(), "rb");
            if (!slow5File_i) {
                ERROR("cannot open %s. skipping...\n", slow5_files[i].c_str());
                continue;
            }
            while ((size = fread(buf, 1, BUFSIZ, slow5File_i))) {
                fwrite(buf, 1, size, slow5File->fp);
            }
            fclose(slow5File_i);

            int del = remove(slow5_files[i].c_str());
            if (del) {
                ERROR("Deleting temporary file %s failed\n", slow5_files[i].c_str());
                perror("");
                return EXIT_FAILURE;
            }
        }



        fprintf(stderr, "[%s] Concatinating blow5s - took %.3fs\n", __func__, slow5_realtime() - realtime0);
    }else{
        INFO("%s", "Using batchmode");
        double time_get_to_mem = 0;
        double time_thread_execution = 0;
        double time_write = 0;

        int64_t batch_size = read_id_batch_capacity;
        size_t slow5_file_index = 0;
        int flag_EOF = 0;

        struct slow5_file *from = slow5_open(slow5_files[slow5_file_index].c_str(), "r");
        if (from == NULL) {
            ERROR("File '%s' could not be opened - %s.", slow5_files[slow5_file_index].c_str(), strerror(errno));
            return EXIT_FAILURE;
        }

        while(1) {

            db_t db = { 0 };
            db.mem_records = (char **) malloc(batch_size * sizeof *db.read_id);
            db.mem_bytes = (size_t *) malloc(batch_size * sizeof *db.read_id);

            int64_t record_count = 0;
            size_t bytes;
            char *mem;
            double realtime = slow5_realtime();
            while (record_count < batch_size) {
                if (!(mem = (char *) slow5_get_next_mem(&bytes, from))) {
                    if (slow5_errno != SLOW5_ERR_EOF) {
                        return EXIT_FAILURE;
                    } else { //EOF file reached
                        flag_EOF = 1;
                        break;
                    }
                } else {
                    db.mem_records[record_count] = mem;
                    db.mem_bytes[record_count] = bytes;
                    record_count++;
                }
            }
            time_get_to_mem += slow5_realtime() - realtime;

            realtime = slow5_realtime();
            // Setup multithreading structures
            core_t core;
            core.num_thread = num_threads;
            core.fp = from;
            core.format_out = format_out;
            core.press_method = pressMethod;
            core.lossy = lossy;
            core.slow5_file_index = slow5_file_index;

            db.n_batch = record_count;
            db.read_record = (raw_record_t*) malloc(record_count * sizeof *db.read_record);
            db.list = list;
            work_db(&core,&db,parallel_reads_model);
            time_thread_execution += slow5_realtime() - realtime;

            realtime = slow5_realtime();
            for (int64_t i = 0; i < record_count; i++) {
                fwrite(db.read_record[i].buffer,1,db.read_record[i].len,slow5File->fp);
                free(db.read_record[i].buffer);
            }
            time_write += slow5_realtime() - realtime;

            // Free everything
            free(db.mem_bytes);
            free(db.mem_records);
            free(db.read_record);

            if(flag_EOF){
                if (slow5_close(from) == EOF) { //close file
                    ERROR("File '%s' failed on closing - %s.", slow5_files[slow5_file_index].c_str(), strerror(errno));
                    return EXIT_FAILURE;
                }
                slow5_file_index++;
                if(slow5_file_index == slow5_files.size()){
                    break;
                }else{
                    from = slow5_open(slow5_files[slow5_file_index].c_str(), "r");
                    if (from == NULL) {
                        ERROR("File '%s' could not be opened - %s.", slow5_files[slow5_file_index].c_str(), strerror(errno));
                        return EXIT_FAILURE;
                    }
                }
            }

        }
        if (meta->verbosity_level >= LOG_DEBUG) {
            DEBUG("time_get_to_mem\t%.3fs", time_get_to_mem);
            DEBUG("time_depress_parse\t%.3fs", time_thread_execution);
            DEBUG("time_write\t%.3fs", time_write);
        }

    }
    if (format_out == SLOW5_FORMAT_BINARY) {
        slow5_eof_fwrite(slow5File->fp);
    }
    slow5_close(slow5File);

    if(delete_directory(output_dir)==EXIT_FAILURE){
        return EXIT_FAILURE;
    }

    EXIT_MSG(EXIT_SUCCESS, argv, meta);
    return EXIT_SUCCESS;
}

int delete_directory(std::string output_dir) {
    int del = rmdir(output_dir.c_str());
    if (del) {
        ERROR("Deleting temp directory failed%s\n", "");
        perror("");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
