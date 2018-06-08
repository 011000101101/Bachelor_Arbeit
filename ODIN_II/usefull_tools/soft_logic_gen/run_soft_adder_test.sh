#!/bin/bash

##############################################
##### TUNING PARAMETER

#weigth of different variables
CRIT_W="1.0"
SIZE_W="1.0"
POWE_W="1.0"


VTR_TASK_TO_HOME=../..
TRACKER_DIR=ODIN_II/usefull_tools/soft_logic_gen/track_completed
cd ../../..
VTR_HOME_DIR=`pwd`
export VTR_HOME_DIR=$VTR_HOME_DIR
INITIAL_BITSIZE=2
END_BITSIZE=128

list_of_arch="\
arch/timing/k6_N10_mem32K_40nm.xml \
arch/timing/k6_frac_N10_mem32K_40nm.xml \
"
function build_parse_file() {
  #build parse file
echo "critical_path_delay;vpr.crit_path.out;Final critical path: (.*) ns" > $VTR_HOME_DIR/$TRACKER_DIR/parse.txt
echo "logic_block_area_total;vpr.out;Total logic block area .*: (.*)">> $VTR_HOME_DIR/$TRACKER_DIR/parse.txt
echo "total_power;*.power;^Total\s+(.*?)\s+" >> $VTR_HOME_DIR/$TRACKER_DIR/parse.txt
}


function get_geomean() {
  ADDERS=$1

  DIV_W=$(echo $CRIT_W $SIZE_W $POWE_W | awk '{ print 1.0*($0+$1+$2) }')
  CRIT_W=$(echo $CRIT_W $DIV_W | awk '{ print 1.0*($0/$1) }')
  SIZE_W=$(echo $SIZE_W $DIV_W | awk '{ print 1.0*($0/$1) }')
  POWE_W=$(echo $POWE_W $DIV_W | awk '{ print 1.0*($0/$1) }')


  OUT_CRIT="1.0"
  OUT_SIZE="1.0"
  OUT_POWE="1.0"
  count=0

  while IFS=, read critical_path_delay logic_block_area_total total_power; do
      OUT_CRIT=$(echo $critical_path_delay $OUT_CRIT | awk '{ print $0*$1 }')
      OUT_SIZE=$(echo $logic_block_area_total $OUT_SIZE | awk '{ print $0*$1 }')
      OUT_POWE=$(echo $total_power $OUT_POWE | awk '{ print $0*$1 }')
      count=$((count+=1))
  done < $ADDERS
  rm $ADDERS
  OUT_CRIT=$(echo $OUT_CRIT $count | awk '{ if($1>0) print $0 ** (1.0/$1); else print 1.0;}')
  OUT_SIZE=$(echo $OUT_SIZE $count | awk '{ if($1>0) print $0 ** (1.0/$1); else print 1.0;}')
  OUT_POWE=$(echo $OUT_POWE $count | awk '{ if($1>0) print $0 ** (1.0/$1); else print 1.0;}')

  WEIGHTED_SUM=$(echo $OUT_CRIT $CRIT_W $OUT_SIZE $SIZE_W $OUT_POWE $POWE_W  | awk '{ print ($0*$1)+($2*$3)+($4*$5) }')

  basename=${ADDERS%.results}
  test_name=$(echo ${basename##*/})

  echo "$test_name,$OUT_CRIT,$OUT_SIZE,$OUT_POWE,$WEIGHTED_SUM" >> $VTR_HOME_DIR/$TRACKER_DIR/result-$BITWIDTH.csv
  echo "$test_name,$WEIGHTED_SUM"
}

#$ADDER_TYPE $BIT_WIDTH MIN_SIZE
#$1          $2         $3
function build_task() {
    ADDER_TYPE=$1
    MIN_SIZE=$(( $2 ))
    MAX_SIZE=$(( $3 ))
    FILL_DEF_TO=$END_BITSIZE

    for NEXT_SIZE in $(eval echo {$MIN_SIZE..$MAX_SIZE}); do
        v_counter=0
        for verilogs in $VTR_HOME_DIR/$TRACKER_DIR/test_$MAX_SIZE/verilogs/*; do
          a_counter=0
          for xmls in $list_of_arch; do
            TEST_DIR=$VTR_HOME_DIR/$TRACKER_DIR/test_$MAX_SIZE/${v_counter}_${a_counter}/${ADDER_TYPE}-${NEXT_SIZE}
            mkdir -p $TEST_DIR

            #get arch and verilog
            cp $VTR_HOME_DIR/vtr_flow/$xmls $TEST_DIR/arch.xml
            cp $verilogs $TEST_DIR/circuit.v
            cp $VTR_HOME_DIR/$TRACKER_DIR/parse.txt $TEST_DIR/parse.txt

            #build odin config
            cat $TRACKER_DIR/current_config.odin > $TEST_DIR/odin.soft_config
            for bit_size in $(eval echo {$MAX_SIZE..$FILL_DEF_TO}); do
              echo "+,$bit_size,soft,$ADDER_TYPE,$NEXT_SIZE" >> $TEST_DIR/odin.soft_config
            done

            a_counter=$((a_counter+1))
          done
          v_counter=$((v_counter+1))
        done
    done
}
#bitwidth
#$1
function run_parralel_tasks() {

  find $VTR_HOME_DIR/$TRACKER_DIR/test_$1/ -maxdepth 2 -mindepth 2 | grep default | xargs -P$((`nproc`*2+1)) -I test_dir /bin/bash -c \
  '
  TEST_DIR=test_dir
  mkdir -p $TEST_DIR/../OUTPUT
  echo "$VTR_HOME_DIR/ODIN_II/odin_II \
    -a $TEST_DIR/arch.xml \
    -V $TEST_DIR/circuit.v \
    -o $TEST_DIR/odin.blif \
    -g 100 \
    -sim_dir $TEST_DIR/../OUTPUT/" > $TEST_DIR/run_result

  $VTR_HOME_DIR/ODIN_II/odin_II \
    -a $TEST_DIR/arch.xml \
    -V $TEST_DIR/circuit.v \
    -o $TEST_DIR/odin.blif \
    -g 100 \
    -sim_dir $TEST_DIR/../OUTPUT/ &>> $TEST_DIR/run_result

  /bin/perl $VTR_HOME_DIR/vtr_flow/scripts/run_vtr_flow.pl \
  $TEST_DIR/odin.blif $TEST_DIR/arch.xml \
  -power \
  -cmos_tech $VTR_HOME_DIR/vtr_flow/tech/PTM_45nm/45nm.xml \
  -temp_dir $TEST_DIR/run \
  -starting_stage abc &>> $TEST_DIR/run_result

  /bin/perl $VTR_HOME_DIR/vtr_flow/scripts/parse_vtr_flow.pl \
  $TEST_DIR/run $TEST_DIR/parse.txt \
  &> $TEST_DIR/run/parse_results.txt

  tail -n +2 $TEST_DIR/run/parse_results.txt > $TEST_DIR/../default.results
  echo "INITIALIZED"
  rm -Rf $TEST_DIR
  '

  # #parralel this START
  find $VTR_HOME_DIR/$TRACKER_DIR/test_$1/ -maxdepth 2 -mindepth 2 | grep -v default | grep -v OUTPUT | xargs -P$((`nproc`*2+1)) -I test_dir /bin/bash -c \
  '
  TEST_DIR=test_dir
  echo "$VTR_HOME_DIR/ODIN_II/odin_II \
    --adder_type $TEST_DIR/odin.soft_config \
    -a $TEST_DIR/arch.xml \
    -V $TEST_DIR/circuit.v \
    -o $TEST_DIR/odin.blif \
    -t $TEST_DIR/../OUTPUT/input_vectors \
    -T $TEST_DIR/../OUTPUT/output_vectors
    -sim_dir $TEST_DIR/ \
  && /bin/perl $VTR_HOME_DIR/vtr_flow/scripts/run_vtr_flow.pl \
      $TEST_DIR/odin.blif $TEST_DIR/arch.xml \
      -power \
      -cmos_tech $VTR_HOME_DIR/vtr_flow/tech/PTM_45nm/45nm.xml \
      -temp_dir $TEST_DIR/run \
      -starting_stage abc" > $TEST_DIR/run_result

  $VTR_HOME_DIR/ODIN_II/odin_II \
      --adder_type $TEST_DIR/odin.soft_config \
      -a $TEST_DIR/arch.xml \
      -V $TEST_DIR/circuit.v \
      -o $TEST_DIR/odin.blif \
      -t $TEST_DIR/../OUTPUT/input_vectors \
      -T $TEST_DIR/../OUTPUT/output_vectors \
      -sim_dir $TEST_DIR/ &>> $TEST_DIR/run_result \
  && /bin/perl $VTR_HOME_DIR/vtr_flow/scripts/run_vtr_flow.pl \
      $TEST_DIR/odin.blif $TEST_DIR/arch.xml \
      -power \
      -cmos_tech $VTR_HOME_DIR/vtr_flow/tech/PTM_45nm/45nm.xml \
      -temp_dir $TEST_DIR/run \
      -starting_stage abc &>> $TEST_DIR/run_result

  my_dir=$(readlink -f $TEST_DIR)
  parent_dir=$(readlink -f $TEST_DIR/..)
  test_bit=$(readlink -f $TEST_DIR/../..)

  if grep -q OK "$TEST_DIR/run_result";then
      /bin/perl $VTR_HOME_DIR/vtr_flow/scripts/parse_vtr_flow.pl \
      $TEST_DIR/run $TEST_DIR/parse.txt \
      &> $TEST_DIR/run/parse_results.txt

      tail -n +2 $TEST_DIR/run/parse_results.txt > $TEST_DIR/../${TEST_DIR##*/}.results
      printf "<${test_bit##*/}-${parent_dir##*/}>\t<${my_dir##*/}> \t\t... PASS\n"
  else
      my_dir=$(readlink -f $TEST_DIR)
      parent_dir=$(readlink -f $TEST_DIR/..)
      test_bit=$(readlink -f $TEST_DIR/../..)
      mv $TEST_DIR $TEST_DIR/../../../failures/${test_bit##*/}${parent_dir##*/}-${my_dir##*/}
      printf "<${test_bit##*/}-${parent_dir##*/}>\t<${my_dir##*/}> \t\t... ERROR\n"
  fi
  rm -Rf $TEST_DIR
  '
  rm -Rf $(find $VTR_HOME_DIR/$TRACKER_DIR/test_$1/ -maxdepth 2 -mindepth 2 | grep OUTPUT)
  #END
}


#bitwidth
#$1
function build_verilogs() {
  filename="$VTR_HOME_DIR/ODIN_II/usefull_tools/soft_logic_gen/verilogs/adder_tree.v"
  OUTPUT_DIR="$VTR_HOME_DIR/$TRACKER_DIR/test_$1/verilogs"
  sed -e "s/%%OPERATOR%%/+/g" -e "s/%%LEVELS_OF_ADDER%%/2/g" -e "s/%%ADDER_BITS%%/$1/g" "$filename" > $OUTPUT_DIR/2L_add_tree.v
  sed -e "s/%%OPERATOR%%/+/g" -e "s/%%LEVELS_OF_ADDER%%/3/g" -e "s/%%ADDER_BITS%%/$1/g" "$filename" > $OUTPUT_DIR/3L_add_tree.v
}


######### inital cleanup ##########

rm -Rf $VTR_HOME_DIR/$TRACKER_DIR
mkdir -p $VTR_HOME_DIR/$TRACKER_DIR/failures

#build files
build_parse_file

[ ! -e $VTR_HOME_DIR/$TRACKER_DIR/current_config.odin ] && touch $VTR_HOME_DIR/$TRACKER_DIR/current_config.odin

START=1
END=$((INITIAL_BITSIZE-1))

for BITWIDTH in $(eval echo {$START..$END}); do
  echo "+,$BITWIDTH,soft,default,$BITWIDTH" >> $VTR_HOME_DIR/$TRACKER_DIR/current_config.odin
done

for BITWIDTH in $(eval echo {$INITIAL_BITSIZE..$END_BITSIZE}); do
  echo ""
  echo "TESTING $BITWIDTH ##########"
  echo ""

  mkdir -p $VTR_HOME_DIR/$TRACKER_DIR/test_$BITWIDTH/verilogs

  build_verilogs $BITWIDTH

  build_task  "default"   $BITWIDTH $BITWIDTH
  build_task  "csla"      "1" $BITWIDTH
  build_task  "bec_csla"  "1" $BITWIDTH

  rm -Rf $VTR_HOME_DIR/$TRACKER_DIR/test_$BITWIDTH/verilogs

  run_parralel_tasks $BITWIDTH

  #get dif for each arch_verilog pair vs default
  for TEST_DIR in $VTR_HOME_DIR/$TRACKER_DIR/test_$BITWIDTH/*; do
    if [ -e $TEST_DIR/default.results ]; then
      #find default_values
      sed -i 's/[\t ]/,/g' $TEST_DIR/default.results
      default_critical=$(cat $TEST_DIR/default.results | cut -d "," -f1)
      default_size=$(cat $TEST_DIR/default.results | cut -d "," -f2)
      default_power=$(cat $TEST_DIR/default.results | cut -d "," -f3)
      echo "1.0,1.0,1.0" >> $TEST_DIR/../default.results

      for results_out in $TEST_DIR/*.results; do
        test_name=${results_out##*/}
        sed -i 's/[\t ]/,/g' $results_out

        temp_critical=$(cat $results_out | cut -d "," -f1)
        temp_size=$(cat $results_out |  cut -d "," -f2)
        temp_power=$(cat $results_out |  cut -d "," -f3)

        echo $temp_critical $default_critical $temp_size $default_size $temp_power $default_power | \
        awk '
        {
          a=($0>0.0 && $1>0.0)?($0/$1):1.0;
          b=($2>0.0 && $3>0.0)?($2/$3):1.0;
          c=($4>0.0 && $5>0.0)?($4/$5):1.0;
          printf "%f,%f,%f\n",$a,$b,$c
        }' >> $TEST_DIR/../$test_name
      done
    fi
    rm -Rf $TEST_DIR
  done

  #get geomean of each adder_type and output to single file
  #keep best adder in memory to output to odin_config
  values=$(get_geomean $VTR_HOME_DIR/$TRACKER_DIR/test_$BITWIDTH/default.results)
  BEST_NAME=$(echo $values | cut -d "," -f1 )
  BEST_VALUE=$(echo $values | cut -d "," -f2 )

  for ADDERS in $VTR_HOME_DIR/$TRACKER_DIR/test_$BITWIDTH/*.results; do
    temp_values=$(get_geomean $ADDERS)
    TEMP_VALUE=$(echo $temp_values | cut -d "," -f2)
    TEMP_NAME=$(echo $temp_values | cut -d "," -f1)

    echo ">$TEMP_NAME,$TEMP_VALUE< | >$BEST_NAME,$BEST_VALUE< "

    if [ "$(echo "$TEMP_VALUE < $BEST_VALUE"  | bc -l)" -ne "0" ]; then
      BEST_NAME=$TEMP_NAME
      BEST_VALUE=$TEMP_VALUE
    fi
  done

  echo "current best :: >$BEST_NAME< with adder >$BEST_VALUE<"
  BEST_NAME=$(echo $BEST_NAME | tr "-" ',')
  echo "+,$BITWIDTH,soft,$BEST_NAME" >> $VTR_HOME_DIR/$TRACKER_DIR/current_config.odin

done
