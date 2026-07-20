program unaligned_equivalence
  implicit none
  integer(kind=1) :: local_bytes(17), common_bytes(17)
  real(kind=8) :: local_values(2), common_values(2)
  real(kind=8) :: snapshot(2)
  character(len=16) :: record
  character(len=128) :: namelist_record
  interface
    subroutine increase_descriptor(values)
      real(kind=8), intent(inout) :: values(:)
    end subroutine increase_descriptor
    real(kind=8) function adjusted(value)
      real(kind=8), intent(in) :: value
    end function adjusted
  end interface
  common /raw_storage/ common_bytes
  equivalence (local_bytes(2), local_values(1))
  equivalence (common_bytes(2), common_values(1))
  namelist /local_state/ local_values

  record = '   1.250   2.750'
  read (record, '(2F8.3)') local_values
  call increase(local_values(2))
  call increase_all(local_values)
  call increase_descriptor(local_values)
  local_values(1) = adjusted(local_values(1))
  snapshot = local_values
  local_values = snapshot
  local_values = [local_values(1), local_values(2)]
  open (unit=17, status='scratch', form='unformatted', action='readwrite')
  write (17) local_values
  rewind (17)
  local_values = 0.0_8
  read (17) local_values
  close (17)
  write (namelist_record, nml=local_state)
  local_values = 0.0_8
  read (namelist_record, nml=local_state)
  call populate_common()
  call replace(common_values(2))

  write (*, '(4(F8.3,1X))') local_values, common_values
end program unaligned_equivalence

subroutine populate_common()
  implicit none
  integer(kind=1) :: common_bytes(17)
  real(kind=8) :: common_values(2)
  character(len=16) :: record
  common /raw_storage/ common_bytes
  equivalence (common_bytes(2), common_values(1))

  record = '3.5 4.75'
  read (record, *) common_values
end subroutine populate_common

subroutine increase(value)
  implicit none
  real(kind=8), intent(inout) :: value

  value = value + 0.5_8
end subroutine increase

subroutine increase_all(values)
  implicit none
  real(kind=8), intent(inout) :: values(2)

  values(1) = values(1) + 0.25_8
  values(2) = values(2) + 0.25_8
end subroutine increase_all

subroutine increase_descriptor(values)
  implicit none
  real(kind=8), intent(inout) :: values(:)

  values = values + 0.125_8
end subroutine increase_descriptor

subroutine replace(value)
  implicit none
  real(kind=8), intent(out) :: value

  value = 6.25_8
end subroutine replace

real(kind=8) function adjusted(value)
  implicit none
  real(kind=8), intent(in) :: value

  adjusted = value + 0.375_8
end function adjusted
