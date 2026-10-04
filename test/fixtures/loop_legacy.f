      program loop_legacy
      implicit none
      integer*8 outer, inner, first, last, stride, total
      first = 4294967296_8
      last = first + 2_8
      stride = 2_8
      total = 0_8
      do 100 outer = first, last, stride
      do 100 inner = 1_8, 2_8
      total = total + outer + inner
  100 continue
      if (total .ne. 17179869194_8) stop 1
      if (outer .ne. first + 4_8) stop 2
      if (inner .ne. 3_8) stop 3
      do 200 outer = first, last, stride
      do 200 inner = 1_8, 2_8
      goto 300
  200 continue
  300 if (outer .ne. first .or. inner .ne. 1_8) stop 4
      print '(A)', 'legacy shared-label loops passed'
      end
