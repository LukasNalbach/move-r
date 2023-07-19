#include <iostream>
#include <filesystem>
#include <move_r/misc/utils.hpp>
#include <move_r/move_r.hpp>

int ptr = 1;
uint16_t p = 1;
bool revert_in_memory = false;
std::string path_index_file;
std::string path_outputfile;
std::string name_textfile;
std::ifstream index_file;
std::ofstream output_file;
std::ofstream measurement_file;

void help(std::string msg) {
	if (msg != "") std::cout << msg << std::endl;
	std::cout << "move-r-revert: reconstruct the original file from the index." << std::endl << std::endl;
	std::cout << "usage: move-r-revert [options] <index_file> <output_file>" << std::endl;
	std::cout << "   -im                        revert in memory; faster, but stores the whole" << std::endl;
	std::cout << "                              input in memory" << std::endl;
	std::cout << "   -p <integer>               number of threads to use while reverting" << std::endl;
	std::cout << "                              (default: greatest possible)" << std::endl;
	std::cout << "   -m <m_file> <text_name>    m_file is the file to write measurement data to," << std::endl;
	std::cout << "                              text_name should be the name of the original file" << std::endl;
	std::cout << "   <index_file>               index file (with extension .move-r)" << std::endl;
	std::cout << "   <output_file>              output file" << std::endl;
	exit(0);
}

void parse_args(char** argv, int argc, int &ptr) {
	std::string s = argv[ptr];
	ptr++;

	if (s == "-m") {
		if (ptr >= argc-1) help("error: missing parameter after -m option");
		std::string path_m_file = argv[ptr++];
		measurement_file.open(path_m_file,std::filesystem::exists(path_m_file) ? std::ios::app : std::ios::out);
		if (!measurement_file.good()) help("error: cannot open measurement file");
		name_textfile = argv[ptr++];
	} else if (s == "-im") {
		revert_in_memory = true;
	} else if (s == "-p") {
		if (ptr >= argc-1) help("error: missing parameter after -p option");
		p = atoi(argv[ptr++]);
		if (p < 1) help("error: p < 1");
		if (p > omp_get_max_threads()) help("error: p > number of available threads");
	} else  {
		help("error: unrecognized '" + s + "' option");
	}
}

template <typename uint_t>
void measure_revert() {
	std::cout << std::setprecision(4);
	std::cout << "loading the index" << std::flush;
	auto t1 = now();
	move_r<uint_t> index;
	index.load(index_file,{revert});
	log_runtime(t1);
	index_file.close();
	std::cout << std::endl;
	index.log_data_structure_sizes();
	std::cout << std::endl;
	std::chrono::steady_clock::time_point t2,t3;
	std::string input;
	p = std::min({(uint16_t)omp_get_max_threads(),index.max_revert_threads(),p});

	if (revert_in_memory) {
		std::cout << "reverting the index in memory using " << format_threads(p) << std::flush;
		t2 = now();
		input = index.revert_range(0,index.input_size()-1,p);
		t3 = now();
	} else {
		std::cout << "reverting the index using " << format_threads(p) << std::flush;
		t2 = now();
		index.revert_range(output_file,0,index.input_size()-1,p);
		t3 = now();
	}
	
	log_runtime(t2,t3);
	uint64_t time_revert = time_diff_ns(t2,t3);

	if (measurement_file.is_open()) {
		measurement_file << "RESULT";
		measurement_file << " type=revert";
		measurement_file << " text=" << name_textfile;
		measurement_file << " index_impl=move_r";
		measurement_file << " a=" << index.balancing_parameter();
		measurement_file << " n=" << index.input_size();
		measurement_file << " sigma=" << std::to_string(index.alphabet_size());
		measurement_file << " r=" << index.num_bwt_runs();
		measurement_file << " r_=" << index.num_intervals_m_lf();
		measurement_file << " r__=" << index.num_intervals_m_phi();
		index.log_data_structure_sizes(measurement_file);
		measurement_file << " time_revert=" << time_revert;
		measurement_file << std::endl;
		measurement_file.close();
	}

	if (revert_in_memory) write_to_file(output_file,input.c_str(),input.size());
}

int main(int argc, char **argv) {
	if (argc < 3) help("");
	while (ptr < argc - 2) parse_args(argv, argc, ptr);

	path_index_file = argv[ptr];
	path_outputfile = argv[ptr+1];

	index_file.open(path_index_file);
	output_file.open(path_outputfile);

	if (!index_file.good()) help("error: could not read <index_file>");
	if (!output_file.good()) help("error: could not create <output_file>");

	bool is_64_bit;
	index_file.read((char*)&is_64_bit,1);
	index_file.seekg(0,std::ios::beg);

	if (is_64_bit) {
		measure_revert<uint64_t>();
	} else {
		measure_revert<uint32_t>();
	}

	output_file.close();
}