program named_constructs
  implicit none
  integer :: i, j, total

  total = 0
  outer: do i = 1, 5
    inner: do j = 1, 5
      transfer_scope: block
        integer, allocatable :: scratch(:)
        allocate(scratch(1))
        scratch(1) = i * 10 + j
        if (j == 2) cycle inner
        if (i == 2 .and. j == 3) cycle outer
        if (i == 4 .and. j == 3) exit outer
        total = total + scratch(1)
      end block transfer_scope
    end do inner
  end do outer

  decision: if (total > 0) then
    total = total + 1000
  else decision
    total = -1
  end if decision

  choice: select case (mod(total, 3))
  case (0) choice
    total = total + 100
  case (1) choice
    total = total + 200
  case default choice
    total = total + 300
  end select choice

  local_scope: block
    integer :: local
    local = 7
    total = total + local
  end block local_scope

  if (total /= 1355) error stop 1
end program named_constructs
