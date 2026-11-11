int main() {
  ui::setNetworkInfo("192.168.4.1", "", "");
  ui::begin(); settings::setActive(30); run(50);
  seq("RR"); shot("on_ch123_tile"); press('S'); shot("number0"); seq("UUR" "UR"); shot("number");
  int s0 = sim.sends; press('S'); run(1500); shot("number_sent"); printf("digits sent: %d (want 3)\n", sim.sends - s0);
  press('B'); seq("L" "L" "L"); shot("wrap_to_allkeys"); press('S'); shot("allkeys"); seq("DD"); s0 = sim.sends; press('S'); printf("allkeys send: %d (want 1)\n", sim.sends - s0); shot("allkeys_sent");
  press('B'); shot("back_on_allkeys_tile");
  return 0;
}
