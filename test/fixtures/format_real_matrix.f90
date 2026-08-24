program format_real_matrix
  implicit none
  character(32) :: input_record
  character(32) :: runtime_format
  character(32) :: round_mode
  character(32) :: output_record
  character(400) :: wide_record
  integer(kind=4) :: bits4
  integer(kind=8) :: bits8
  integer :: io_status
  real(kind=4) :: single_value
  real(kind=8) :: special_value
  real(kind=8) :: wide_value

  write(*, 100) 12.5d0, 12.5d0, 12.5d0, 12.5d0, 12.5d0
  write(*, 100) 0.0125d0, 0.0125d0, 0.0125d0, 0.0125d0, 0.0125d0
  write(*, 100) 999.95d0, 999.95d0, 999.95d0, 999.95d0, 999.95d0
  write(*, 100) -0.0d0, -0.0d0, -0.0d0, -0.0d0, -0.0d0
  write(*, 100) huge(0.0d0), huge(0.0d0), huge(0.0d0), &
    huge(0.0d0), huge(0.0d0)
  write(*, 110) 12.5d0, 12.5d0, 12.5d0, 12.5d0
  write(*, 120) 12.5d0, 12.5d0
  write(*, '("exp0 |",E12.4E0,"|")') 12.5d0
  runtime_format = '("dynamic exp0 |",E12.4E0,"|")'
  write(*, runtime_format) 12.5d0

  write(*, 130) 1.25d0, 1.35d0, -1.25d0, -1.35d0
  runtime_format = '("RC |",RC,4(F8.1,"|"))'
  write(*, runtime_format) 1.25d0, 1.35d0, -1.25d0, -1.35d0
  write(*, 140) 1.25d0, 1.35d0, -1.25d0, -1.35d0
  write(*, 150) 1.25d0, 1.35d0, -1.25d0, -1.35d0
  write(*, 160) 1.25d0, 1.35d0, -1.25d0, -1.35d0
  write(*, 170) 1.25d0, 1.35d0, -1.25d0, -1.35d0
  write(*, '("restore |",RU,F8.1,"|",RP,F8.1,"|")') 1.25d0, 1.25d0
  write(*, fmt='("statement |",F8.1,"|")', round='up', &
    decimal='comma', sign='plus') 1.25d0
  round_mode = 'down'
  write(*, fmt='("dynamic round |",F8.1,"|")', round=round_mode) 1.25d0
  round_mode = 'invalid'
  output_record = 'preserved'
  write(output_record, fmt='(F8.1)', round=round_mode, iostat=io_status) 1.25d0
  if (io_status == 0) error stop

  write(*, 180) 1234.0d0, 1.0d100, 1.0d100, 1.0d100
  write(*, '("compact |",F3.2,"|",E7.2,"|")') 0.125d0, 1.0d100
  write(*, '("g0 wide |",4(G0,"|"))') 12.5d0, 0.1d0, huge(0.0d0), -0.0d0
  write(*, '("g0 single |",3(G0,"|"))') 12.5, 0.1, huge(0.0)

  input_record = 'Infinity'
  read(input_record, '(G32.16)') special_value
  write(*, '("special |",E18.4,"|")') special_value
  input_record = '-Infinity'
  read(input_record, '(G32.16)') special_value
  write(*, '("special |",E18.4,"|")') special_value
  input_record = 'NaN'
  read(input_record, '(G32.16)') special_value
  write(*, '("special |",E18.4,"|")') special_value

  input_record = '0.1'
  read(input_record, '(RN,F3.1)') wide_value
  bits8 = transfer(wide_value, bits8)
  write(*, '("input RN |",Z16.16,"|")') bits8
  read(input_record, '(RU,F3.1)') wide_value
  bits8 = transfer(wide_value, bits8)
  write(*, '("input RU |",Z16.16,"|")') bits8
  read(input_record, '(RD,F3.1)') wide_value
  bits8 = transfer(wide_value, bits8)
  write(*, '("input RD |",Z16.16,"|")') bits8
  read(input_record, '(RZ,F3.1)') single_value
  bits4 = transfer(single_value, bits4)
  write(*, '("input RZ single |",Z8.8,"|")') bits4
  input_record = '-0.1'
  read(input_record, '(RU,F4.1)') wide_value
  bits8 = transfer(wide_value, bits8)
  write(*, '("input -RU |",Z16.16,"|")') bits8
  read(input_record, '(RD,F4.1)') wide_value
  bits8 = transfer(wide_value, bits8)
  write(*, '("input -RD |",Z16.16,"|")') bits8
  wide_record = '0.1'
  read(wide_record, '(RN,F400.1)') wide_value
  bits8 = transfer(wide_value, bits8)
  write(*, '("input wide |",Z16.16,"|")') bits8

100 format('E=|',E18.4,'| E3=|',E18.4E3,'| ES=|',ES18.4, &
  '| EN=|',EN18.4,'| G=|',G18.4,'|')
110 format('scale |',0P,E18.4,'|',1P,E18.4,'|',2P,E18.4, &
  '|',-1P,E18.4,'|')
120 format('es scale |',2P,ES18.4,'| en |',EN18.4,'|')
130 format('RN |',RN,4(F8.1,'|'))
140 format('RU |',RU,4(F8.1,'|'))
150 format('RD |',RD,4(F8.1,'|'))
160 format('RZ |',RZ,4(F8.1,'|'))
170 format('RP |',RP,4(F8.1,'|'))
180 format('overflow |',F4.1,'|',E7.2,'|',E9.2E4,'|',E20.2E2,'|')
end program format_real_matrix
