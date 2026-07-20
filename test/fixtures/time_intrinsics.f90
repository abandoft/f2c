program time_intrinsics
  implicit none
  integer(kind=8) :: values(9), count_before, count_after, count_max
  integer(kind=2) :: compact_values(10), compact_count, compact_rate, compact_max
  real(kind=8) :: count_rate, cpu_before, cpu_after
  character(len=8) :: date_text
  character(len=10) :: time_text, omitted_time
  character(len=5) :: zone_text
  character(len=4) :: short_date
  character(len=12) :: date_holder
  logical :: date_valid, omitted_valid, tail_preserved
  logical :: clock_valid, compact_valid, cpu_valid

  values = -1_8
  values(9) = 777_8
  call date_and_time(date_text, time_text, zone_text, values)
  date_valid = verify(date_text, '0123456789') == 0 .and. &
               verify(time_text(1:6), '0123456789') == 0 .and. time_text(7:7) == '.' .and. &
               verify(time_text(8:10), '0123456789') == 0 .and. &
               values(1) >= 1 .and. values(2) >= 1 .and. values(2) <= 12 .and. &
               values(3) >= 1 .and. values(3) <= 31 .and. abs(values(4)) <= 14 * 60 .and. &
               values(5) >= 0 .and. values(5) <= 23 .and. &
               values(6) >= 0 .and. values(6) <= 59 .and. &
               values(7) >= 0 .and. values(7) <= 60 .and. &
               values(8) >= 0 .and. values(8) <= 999
  date_valid = date_valid .and. (zone_text == '     ' .or. &
               ((zone_text(1:1) == '+' .or. zone_text(1:1) == '-') .and. &
                verify(zone_text(2:5), '0123456789') == 0))
  tail_preserved = values(9) == 777_8

  compact_values = -1_2
  date_holder = 'XXXXXXXXXXXX'
  call date_and_time(time=omitted_time, values=compact_values(2:9))
  call date_and_time(date=short_date)
  call date_and_time(date=date_holder(3:10))
  omitted_valid = verify(omitted_time(1:6), '0123456789') == 0 .and. &
                  omitted_time(7:7) == '.' .and. &
                  verify(short_date, '0123456789') == 0 .and. compact_values(2) >= 1 .and. &
                  compact_values(1) == -1_2 .and. compact_values(10) == -1_2 .and. &
                  date_holder(1:2) == 'XX' .and. date_holder(11:12) == 'XX' .and. &
                  verify(date_holder(3:10), '0123456789') == 0

  call system_clock(count_before, count_rate, count_max)
  call system_clock(count=count_after)
  clock_valid = count_rate > 0.0_8 .and. count_max > 0_8 .and. &
                count_before >= 0_8 .and. count_before <= count_max .and. &
                count_after >= 0_8 .and. count_after <= count_max
  call system_clock(compact_count, compact_rate, compact_max)
  compact_valid = (compact_rate > 0_2 .and. compact_max > 0_2 .and. &
                   compact_count >= 0_2 .and. compact_count <= compact_max) .or. &
                  (compact_count < 0_2 .and. compact_rate == 0_2 .and. compact_max == 0_2)

  call cpu_time(cpu_before)
  call cpu_time(cpu_after)
  cpu_valid = (cpu_before >= 0.0_8 .and. cpu_after >= 0.0_8) .or. &
              (cpu_before < 0.0_8 .and. cpu_after < 0.0_8)

  write (*, '(6(L1,1X))') date_valid, omitted_valid, tail_preserved, &
                           clock_valid, compact_valid, cpu_valid
end program time_intrinsics
