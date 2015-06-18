
(function(plugin){
  plugin.name = "Yandex Music";
  plugin.url = "https://music.yandex.ru/";

  plugin.actions = {
    hello: function(arg1){console.log("Hello man!", arg1);},
    play: function(){},
    stop: function(){},
    next: function(){},
    clearPage: function(){},
  }
}(WebMusicPlugin));
