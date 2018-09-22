#!/bin/bash
#1
trap ctrl_c INT
SHELL=/bin/bash

fail_count=0

function exit_program() {

    if [ -e regression_test/runs/failure.log ]
    then
	    line_count=$(wc -l < regression_test/runs/failure.log)
		fail_count=$[ fail_count+line_count ]
	fi

	if [ $fail_count -gt "0" ]
	then
		echo "Failed $fail_count"
		echo "View Failure log in ODIN_II/regression_test/runs/failure.log, "
	else
		echo "no run failure!"
	fi

	exit $fail_count
}

function ctrl_c() {

	echo ""
	echo "** EXITED FORCEFULLY **"
    fail_count=123456789
	exit_program
}

function micro_test() {
	for benchmark in regression_test/benchmark/micro/*.v
	do
		basename=${benchmark%.v}
		test_name=${basename##*/}
		input_vectors="$basename"_input
		output_vectors="$basename"_output
		DIR="regression_test/runs/micro/$test_name" && mkdir -p $DIR/arch && mkdir -p $DIR/no_arch
	done


	#verilog
	ls regression_test/runs/micro | xargs -n1 -P$1 -I test_dir /bin/bash -c \
	'./odin_II -E \
    --adder_type default \
		-V regression_test/benchmark/micro/test_dir.v \
		-t regression_test/benchmark/micro/test_dir_input \
		-T regression_test/benchmark/micro/test_dir_output \
		-o regression_test/runs/micro/test_dir/no_arch/test_dir.blif \
		-sim_dir regression_test/runs/micro/test_dir/no_arch/ \
		&> regression_test/runs/micro/test_dir/no_arch/log \
	&& ./odin_II -E \
    --adder_type default \
		-b regression_test/runs/micro/test_dir/no_arch/test_dir.blif \
		-t regression_test/benchmark/micro/test_dir_input \
		-T regression_test/benchmark/micro/test_dir_output \
		-sim_dir regression_test/runs/micro/test_dir/no_arch/ \
		&> regression_test/runs/micro/test_dir/no_arch/log \
	&& ./odin_II -E \
    --adder_type default \
		-a ../libs/libarchfpga/arch/sample_arch.xml \
		-V regression_test/benchmark/micro/test_dir.v \
		-t regression_test/benchmark/micro/test_dir_input \
		-T regression_test/benchmark/micro/test_dir_output \
		-o regression_test/runs/micro/test_dir/arch/test_dir.blif \
		-sim_dir regression_test/runs/micro/test_dir/arch/ \
		&> regression_test/runs/micro/test_dir/arch/log \
	&& ./odin_II -E \
    --adder_type default \
		-a ../libs/libarchfpga/arch/sample_arch.xml \
		-b regression_test/runs/micro/test_dir/arch/test_dir.blif \
		-t regression_test/benchmark/micro/test_dir_input \
		-T regression_test/benchmark/micro/test_dir_output \
		-sim_dir regression_test/runs/micro/test_dir/arch/ \
		&> regression_test/runs/micro/test_dir/arch/log \
	&& echo " --- PASSED == micro/test_dir" \
	|| (echo " -X- FAILED == micro/test_dir" \
		&& echo micro/test_dir >> regression_test/runs/failure.log )'

}

#1
#bench
function regression_test() {

	mkdir -p regression_test/runs/full
	mkdir -p OUTPUT

	for test_verilog in regression_test/benchmark/full/*.v
	do

		rm -Rf OUTPUT/*

		basename=${test_verilog%.v}
		test_name=${basename##*/}
		input_vectors="$basename"_input
		output_vectors="$basename"_output
		output_blif="".blif
		DIR="regression_test/runs/full/$test_name" && mkdir -p $DIR


    echo "./odin_II -E \
		--adder_type default \
			-a ../libs/libarchfpga/arch/sample_arch.xml \
			-V $test_verilog \
			-t $input_vectors \
			-T $output_vectors \
			-o regression_test/runs/full/$test_name/$test_name.blif \
			-sim_dir regression_test/runs/full/$test_name/" \
				> regression_test/runs/full/$test_name/log

		./odin_II -E \
    		--adder_type "default" \
			-a "../libs/libarchfpga/arch/sample_arch.xml" \
			-V "$test_verilog" \
			-t "$input_vectors" \
			-T "$output_vectors" \
			-o regression_test/runs/full/$test_name/$test_name.blif \
			-sim_dir regression_test/runs/full/$test_name/ \
			&>> regression_test/runs/full/log \
		&& echo " --- PASSED == full/$test_name" \
		|| (echo " -X- FAILED == full/$test_name" \
		&& echo full/$test_name >> regression_test/runs/failure.log )

	done
}

#1				#2
#benchmark dir	N_trhead
function arch_test() {
	for benchmark in regression_test/benchmark/arch/*.v
	do
    	basename=${benchmark%.v}
    	test_name=${basename##*/}
		DIR="regression_test/runs/arch/$test_name" && mkdir -p $DIR

	done

	ls regression_test/runs/arch | xargs -P$1 -I test_dir /bin/bash -c \
		' ./odin_II -E -V regression_test/benchmark/arch/test_dir.v \
        	--adder_type default \
			-o regression_test/runs/arch/test_dir/test_dir.blif \
			-sim_dir regression_test/runs/arch/test_dir/ \
			&> regression_test/runs/arch/test_dir/log \
    	&& echo " --- PASSED == arch/test_dir" \
    	|| (echo " -X- FAILED == arch/test_dir" \
    		&& echo arch/test_dir >> regression_test/runs/failure.log)'
}

#1				#2
#benchmark dir	N_trhead
function syntax_test() {
	for benchmark in regression_test/benchmark/syntax/*.v
	do
    	basename=${benchmark%.v}
    	test_name=${basename##*/}
		DIR="regression_test/runs/syntax/$test_name" && mkdir -p $DIR

	done

	ls regression_test/runs/syntax | xargs -P$1 -I test_dir /bin/bash -c \
		' ./odin_II -E -V regression_test/benchmark/syntax/test_dir.v \
        	--adder_type default \
			-o regression_test/runs/syntax/test_dir/test_dir.blif \
			-sim_dir regression_test/runs/syntax/test_dir/ \
				&> regression_test/runs/syntax/test_dir/log \
    	&& echo " --- PASSED == syntax/test_dir" \
    	|| (echo " -X- FAILED == syntax/test_dir" \
    		&& echo syntax/test_dir >> regression_test/runs/failure.log)'
}

#1				#2
#benchmark dir	N_trhead
function other_test() {
	for benchmark in regression_test/benchmark/other/*
	do
    	test_name=${benchmark##*/}
		DIR="regression_test/runs/other/$test_name" && mkdir -p $DIR

		eval $(cat regression_test/benchmark/other/$test_name/odin.args | tr '\n' ' ') \
		&> regression_test/runs/other/$test_name/log \
		&& echo " --- PASSED == other/$test_name" \
		|| (echo " -X- FAILED == other/$test_name" \
			&& echo other/$test_name >> regression_test/runs/failure.log)

	done
}


### starts here

#in MB
NB_OF_PROC=1

if [[ "$2" -gt "0" ]]
then
	NB_OF_PROC=$2
	echo "Trying to run benchmark on $NB_OF_PROC processes"
fi


rm -Rf regression_test/runs/*
touch regression_test/runs/failure.log

START=$(date +%s%3N)

case $1 in

	"arch")
		arch_test $NB_OF_PROC
		;;

	"syntax")
		syntax_test $NB_OF_PROC
		;;

	"micro")
		micro_test $NB_OF_PROC
		;;

	"regression")
		regression_test $NB_OF_PROC
		;;

	"other")
		other_test $NB_OF_PROC
		;;

	"full_suite")
		arch_test $NB_OF_PROC
		syntax_test $NB_OF_PROC
		other_test $NB_OF_PROC
		micro_test $NB_OF_PROC
		regression_test $NB_OF_PROC
		;;

	"vtr_basic")
		cd ..
		/usr/bin/perl run_reg_test.pl -j $NB_OF_PROC vtr_reg_basic
		cd ODIN_II
		;;

	"vtr_strong")
		cd ..
		/usr/bin/perl run_reg_test.pl -j $NB_OF_PROC vtr_reg_strong
		cd ODIN_II
		;;

	"pre_commit")
		arch_test $NB_OF_PROC
		syntax_test $NB_OF_PROC
		other_test $NB_OF_PROC
		micro_test $NB_OF_PROC
		regression_test $NB_OF_PROC
		cd ..
		/usr/bin/perl run_reg_test.pl -j $NB_OF_PROC vtr_reg_basic
		/usr/bin/perl run_reg_test.pl -j $NB_OF_PROC vtr_reg_strong
		cd ODIN_II
		;;

	*)
		echo 'nothing to run, ./verify_odin [ arch, syntax, micro, regression, vtr_basic, vtr_strong or pre_commit ] [ nb_of_process ]'
		;;
esac

END=$(date +%s%3N)
echo "ran test in: $(( ((END-START)/1000)/60 )):$(( ((END-START)/1000)%60 )).$(( (END-START)%1000 )) [m:s.ms]"
exit_program
### end here
