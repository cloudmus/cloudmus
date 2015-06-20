
(function(plugin){
 
try {
  var isPlaying = function() {
    if ($(".body_show-pause").length > 0) {
        return true;
    } else {
        return false;
    }
  };
  
  plugin.actions.pause.setVisible(false);
  plugin.actions.stop.setVisible(false);
  
  var updateState = function() {
    if (isPlaying()) {
        plugin.actions.play.setVisible(false);
        plugin.actions.pause.setVisible(true);
    } else {
        plugin.actions.play.setVisible(true);
        plugin.actions.pause.setVisible(false);
    }
  };
  
  $(".player-controls__btn_play").click(updateState);
  $(".player-controls__btn_pause").click(updateState);
  $(".player-controls__btn_next").click(updateState);
  
  plugin.actions.play.triggered.connect(function(){
    $(".player-controls__btn_play").click();
  });
  
  plugin.actions.pause.triggered.connect(function(){
    $(".player-controls__btn_pause").click();
  });
  
  plugin.actions.next.triggered.connect(function(){
    $(".player-controls__btn_next").click();
  });
  
  plugin.addAction("gogogo", "Click me!", "edit-undo", function(){
    alert("gogogo") ;
  });
  
  $(".overhead__content").remove();
  $(".overhead").remove();
  $(".sidebar__placeholder").remove();
  
  console.log("Service 'Yandex Music' loaded");
  console.log(plugin);
  
} 
catch(e) {
  console.error("Service 'Yandex Music' exception:", e);
}
  
}(CloudmusService));
