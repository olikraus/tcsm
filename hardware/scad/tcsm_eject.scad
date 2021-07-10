/*

  tcsm_eject.scad
  
  2021 (c) olikraus@gmail.com

  This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
  To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/.


  Problems 7 Mar 2021
  - Wheel is too high   --> fixed by 1 mm
  - Card gap is too small --> fixed by 1 mm
  - Both arcs are brocken --> fixed with some additional edge corner
  - motor holder block mount screw diameter too small --> fixed by 0.1mm
  - Reduced height of the motor holder (for faster printing)  --> done
  - Leave gap at the lower end of the rail --> done
  14 Mar 2021
  - Both arcs are still brocken --> reduced edge corner (because of sorter rail), added outer support
  
  21 Mar 2021
  - Bugfix: Added pile_gap --> done
  - Added slope to motor mount block --> done
  
   22 Mar 2021
  - Motor must be higher --> 10m --> done
  - lower the support towards sorter house --> done

   24 Mar 2021
   - Increased eject house height to 125 (130 with pile holder)
   
   5 Apr 2021
   - The eject house should be longer, so that the
     wheel is more at the upper end of the cards
     --> Done

   4 Jul 2021
   - Increase slope from 22 to 23 --> Done
   - Extend the eject hourse from 14 to 19, so that
     the wheel is even more at the end of the cards --> DONE


*/


include <trading_card_compare_machine.scad>;

eject_house(false);

