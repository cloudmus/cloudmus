
(function(plugin){
  plugin.name = "Yandex Music";
  plugin.url = "https://music.yandex.ru/";

  plugin.actions = {
    hello: function(arg1){console.log("Hello man!", arg1);},
    play: function(){alert("play music!");},
    stop: function(){},
    next: function(){},
    clearPage: function(){},
  }
  
  plugin.QObject.addAction("hello action!", "edit-undo", "hello");
  plugin.QObject.addAction("play", "edit-undo", "play");
  
  
}(WebMusicPlugin));
