#include <gtest/gtest.h>
#include <move_r/move_r.hpp>

TEST(test_move_r,fuzzy_test) {
    std::random_device rd;
    std::mt19937 gen(rd());
    uint16_t max_num_threads = omp_get_max_threads();

    std::lognormal_distribution<double> avg_input_rep_length_distrib(4.0,2.0);
    std::uniform_real_distribution<double> prob_distrib(0.0,1.0);
    std::uniform_int_distribution<uint8_t> alphabet_size_distrib(1,252);
    std::uniform_int_distribution<uint8_t> uchar_distrib(0,255);
    std::uniform_int_distribution<uint32_t> input_size_distrib(1,200000);
    std::uniform_int_distribution<uint16_t> num_threads_distrib(1,max_num_threads);
    std::lognormal_distribution<double> a_distrib(2.0,3.0);

    uint32_t input_size;
    uint8_t alphabet_size;
    std::string input;
    std::string input_reverted;
    std::string bwt;
    std::string bwt_retrieved;
    std::vector<uint8_t> map_uchar;
    std::vector<uint8_t> unmap_uchar;
    std::vector<int32_t> suffix_array;
    std::vector<int32_t> suffix_array_retrieved;
    uint32_t max_pattern_length;
    uint32_t num_queries;

    auto start_time = now();

    // generate random input strings and test all operations until one hour has passed
    while (time_diff_min(start_time,now()) < 60) {
        // choose a random input length
        input_size = input_size_distrib(gen);
        input.resize(input_size);

        // choose a random alphabet size
        alphabet_size = alphabet_size_distrib(gen);

        // choose a random alphabet
        std::vector<uint8_t> alphabet;
        uint8_t uchar;
        for (uint32_t i=0; i<alphabet_size; i++) {
            do {uchar = uchar_distrib(gen);} while (contains(alphabet,uchar));
            alphabet.push_back(uchar);
        }

        // choose a random input based on the alphabet
        std::uniform_int_distribution<uint8_t> char_idx_distrib(0,alphabet_size-1);
        double avg_input_rep_length = 1.0+avg_input_rep_length_distrib(gen);
        uint8_t cur_uchar = alphabet[char_idx_distrib(gen)];
        for (uint32_t i=0; i<input_size; i++) {
            if (prob_distrib(gen) < 1/avg_input_rep_length) cur_uchar = alphabet[char_idx_distrib(gen)];
            input[i] = uchar_to_char(cur_uchar);
        }

        // build move-r and choose a random number of threads and balancing parameter, but always use libsais,
        // because there are bugs in Big-BWT that come through during fuzzing but not really in practice
        move_r<uint32_t> index(input,{
            .mode = _libsais,
            .num_threads = num_threads_distrib(gen),
            .a = std::min<uint16_t>(2+a_distrib(gen),32767)
        });

        // check if L' can be reconstructed from RS_L'
        #pragma omp parallel for num_threads(max_num_threads)
        for (uint32_t i=0; i<index.M_LF().num_intervals(); i++) EXPECT_EQ(index.L_(i),index.RS_L_()[i]);
        
        // revert the index and compare the output with the input string
        input_reverted = index.revert({.num_threads = num_threads_distrib(gen)});
        #pragma omp parallel for num_threads(max_num_threads)
        for (uint32_t i=0; i<=input_size; i++) EXPECT_EQ(input[i],input_reverted[i]);

        // retrieve the suffix array and compare it with the correct suffix array; if the input contains 0 or 1,
        // then temporarily remap the characters of the input string s.t. it does not contain 0 or 1
        if (contains(alphabet,(uint8_t)0) || contains(alphabet,(uint8_t)1)) {
            map_uchar.resize(256,0);
            unmap_uchar.resize(256,0);
            uint8_t next_uchar = 2;
            for (uint16_t i=0; i<256; i++) {
                if (contains(alphabet,(uint8_t)i)) {
                    map_uchar[i] = next_uchar;
                    unmap_uchar[next_uchar] = i;
                    next_uchar++;
                }
            }
            for (uint32_t i=0; i<input_size; i++) input[i] = uchar_to_char(map_uchar[char_to_uchar(input[i])]);
        }
        input.push_back(uchar_to_char((uint8_t)1));
        suffix_array.resize(input_size+1);
        libsais_omp((uint8_t*)&input[0],&suffix_array[0],input_size+1,0,NULL,max_num_threads);
        if (contains(alphabet,(uint8_t)0) || contains(alphabet,(uint8_t)1)) {
            for (uint32_t i=0; i<input_size; i++) input[i] = uchar_to_char(unmap_uchar[char_to_uchar(input[i])]);
            map_uchar.clear();
            unmap_uchar.clear();
        }
        suffix_array_retrieved.resize(input_size+1);
        index.SA([&suffix_array_retrieved](auto i,auto s){suffix_array_retrieved[i] = s;},{
            .num_threads = num_threads_distrib(gen)
        });
        #pragma omp parallel for num_threads(max_num_threads)
        for (uint32_t i=0; i<=input_size; i++) EXPECT_EQ(suffix_array[i],suffix_array_retrieved[i]);
        
        // compute each suffix array value separately and check if it is correct
        #pragma omp parallel for num_threads(max_num_threads)
        for (uint32_t i=0; i<=input_size; i++) EXPECT_EQ(index.SA(i),suffix_array[i]);

        // retrieve the bwt and compare it with the correct bwt
        bwt.resize(input_size+1);
        #pragma omp parallel for num_threads(max_num_threads)
        for (uint32_t i=0; i<=input_size; i++) {bwt[i] = suffix_array[i] == 0 ? 0 : input[suffix_array[i]-1];}
        bwt_retrieved = index.BWT({.num_threads = num_threads_distrib(gen)});
        #pragma omp parallel for num_threads(max_num_threads)
        for (uint32_t i=0; i<=input_size; i++) EXPECT_EQ(bwt[i],bwt_retrieved[i]);

        // compute each bwt character separately and check if it is correct
        #pragma omp parallel for num_threads(max_num_threads)
        for (uint32_t i=0; i<=input_size; i++) EXPECT_EQ(index.BWT(i),bwt[i]);

        // generate patterns from the input and test count- and locate queries
        std::uniform_int_distribution<> pattern_pos_distrib(0,input_size-1);
        max_pattern_length = std::min<uint32_t>(10000,std::max<uint32_t>(100,input_size/1000));
        std::uniform_int_distribution<> pattern_length_distrib(1,max_pattern_length);
        num_queries = std::min<uint32_t>(10000,std::max<uint32_t>(1000,input_size/100));
        num_queries = std::max<uint32_t>(1,num_queries/max_num_threads);
        #pragma omp parallel num_threads(max_num_threads)
        {
            uint32_t pattern_pos;
            uint32_t pattern_length;
            std::string pattern;
            std::vector<uint32_t> correct_occurrences;
            std::vector<uint32_t> occurrences;
            bool match;
            for(uint32_t cur_query=0; cur_query<num_queries; cur_query++) {
                pattern_pos = pattern_pos_distrib(gen);
                pattern_length = std::min<uint32_t>(input_size-pattern_pos,pattern_length_distrib(gen));
                pattern.resize(pattern_length);
                std::copy(&input[pattern_pos],&input[pattern_pos+pattern_length],&pattern[0]);
                for (uint32_t i=0; i<pattern_length; i++) pattern[i] = input[pattern_pos+i];
                for (uint32_t i=0; i<=input_size-pattern_length; i++) {
                    match = true;
                    for (uint32_t j=0; j<pattern_length; j++) {
                        if (input[i+j] != pattern[j]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) correct_occurrences.emplace_back(i);
                }
                EXPECT_EQ(index.count(pattern),correct_occurrences.size());
                occurrences = index.locate(pattern);
                ips4o::sort(occurrences.begin(),occurrences.end());
                EXPECT_EQ(occurrences,correct_occurrences);
                correct_occurrences.clear();
            }
        }
    }
}