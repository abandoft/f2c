program implicit_mapping_driver
  implicit none
  integer :: output(2)
  integer :: index_value
  character(len=4) :: source, character_output

  source = 'F2C!'
  call implicit_values(output)
  call implicit_character(source, character_output)
  write (*, '(I0,1X,I0,1X,I0,1X,A4)') &
    output(1), output(2), index_value(), character_output
end program implicit_mapping_driver
