// Read just the metadata elements, seeking past audio clusters.
use std::fs::File;
use std::io::{self, Read, Seek, SeekFrom};

use crate::metadata::Metadata;

fn vint(reader: &mut impl Read, keep_marker: bool) -> io::Result<(u64, bool)> {
    let mut first = [0];
    reader.read_exact(&mut first)?;
    let bits = first[0].leading_zeros();
    if bits == 8 {
        return Err(io::ErrorKind::InvalidData.into());
    }
    let mut value = u64::from(if keep_marker {
        first[0]
    } else {
        first[0] & (0x7f >> bits)
    });
    for _ in 0..bits {
        reader.read_exact(&mut first)?;
        value = (value << 8) | u64::from(first[0]);
    }
    Ok((
        value,
        !keep_marker && value == (1u64 << (7 * (bits + 1))) - 1,
    ))
}

fn header(reader: &mut impl Read) -> io::Result<(u64, u64, bool)> {
    let (id, _) = vint(reader, true)?;
    let (size, unknown) = vint(reader, false)?;
    Ok((id, size, unknown))
}

fn elements(mut input: &[u8]) -> impl Iterator<Item = (u64, &[u8])> {
    std::iter::from_fn(move || {
        let (id, size, unknown) = header(&mut input).ok()?;
        let size = if unknown {
            input.len()
        } else {
            usize::try_from(size).ok()?
        };
        let value = input.get(..size)?;
        input = &input[size..];
        Some((id, value))
    })
}

fn text(value: &[u8]) -> String {
    String::from_utf8_lossy(value)
        .trim_end_matches('\0')
        .trim()
        .to_string()
}

fn integer(value: &[u8]) -> u64 {
    value
        .iter()
        .take(8)
        .fold(0, |num, byte| (num << 8) | u64::from(*byte))
}

fn info(data: &[u8], out: &mut Metadata) {
    let mut scale = 1_000_000;
    let mut duration = 0.;
    for (id, value) in elements(data) {
        match id {
            0x2AD7B1 => scale = integer(value).max(1),
            0x4489 => {
                duration = match value.len() {
                    4 => f32::from_be_bytes(value.try_into().unwrap()) as f64,
                    8 => f64::from_be_bytes(value.try_into().unwrap()),
                    _ => 0.,
                };
            }
            0x7BA9 if out.title.is_empty() => out.title = text(value),
            _ => {}
        }
    }
    out.duration_ms = (duration * scale as f64 / 1_000_000.) as u64;
}

fn tags(data: &[u8], out: &mut Metadata) {
    let mut album_title = String::new();
    for (_, tag) in elements(data).filter(|(id, _)| *id == 0x7373) {
        let level = elements(tag)
            .find(|(id, _)| *id == 0x63C0)
            .and_then(|(_, targets)| elements(targets).find(|(id, _)| *id == 0x68CA))
            .map(|(_, value)| integer(value))
            .unwrap_or(30);
        for (_, simple) in elements(tag).filter(|(id, _)| *id == 0x67C8) {
            let mut name = String::new();
            let mut value = String::new();
            for (id, field) in elements(simple) {
                match id {
                    0x45A3 => name = text(field).to_ascii_uppercase(),
                    0x4487 => value = text(field),
                    _ => {}
                }
            }
            if value.is_empty() {
                continue;
            }
            match (level, name.as_str()) {
                (50, "TITLE") if album_title.is_empty() => album_title = value,
                (0..=49, "TITLE") if out.title.is_empty() => out.title = value,
                (0..=49, "ARTIST") if !out.artists.contains(&value) => out.artists.push(value),
                (0..=49, "ALBUM") if out.album.is_empty() => out.album = value,
                _ => {}
            }
        }
    }
    if out.album.is_empty() {
        out.album = album_title;
    }
}

fn attachments(data: &[u8], out: &mut Metadata) {
    for (_, file) in elements(data).filter(|(id, _)| *id == 0x61A7) {
        let mime = elements(file)
            .find(|(id, _)| *id == 0x4660)
            .map(|(_, value)| text(value));
        if mime.is_some_and(|mime| mime.to_ascii_lowercase().starts_with("image/")) {
            if let Some((_, bytes)) =
                elements(file).find(|(id, bytes)| *id == 0x465C && !bytes.is_empty())
            {
                out.cover = Some(bytes.to_vec());
                break;
            }
        }
    }
}

fn codec(data: &[u8]) -> Option<String> {
    let id = elements(data)
        .filter(|(id, _)| *id == 0xAE)
        .find_map(|(_, track)| elements(track).find(|(id, _)| *id == 0x86))
        .map(|(_, value)| text(value))?;
    let base = id
        .trim_start_matches("A_")
        .split('/')
        .next()?
        .to_ascii_lowercase();
    Some(if base == "mpeg" {
        if id.ends_with("L3") { "mp3" } else { "mp2" }.to_owned()
    } else {
        base
    })
}

pub fn read(file: &mut File, out: &mut Metadata, cover: bool) -> io::Result<()> {
    let (id, size, unknown) = header(file)?;
    if id != 0x1A45DFA3 || unknown {
        return Ok(());
    }
    file.seek(SeekFrom::Current(
        i64::try_from(size).map_err(|_| io::ErrorKind::InvalidData)?,
    ))?;
    let (id, size, unknown) = header(file)?;
    if id != 0x18538067 {
        return Ok(());
    }
    let end = if unknown {
        file.metadata()?.len()
    } else {
        file.stream_position()?.saturating_add(size)
    };
    while file.stream_position()? < end {
        let (id, size, unknown) = header(file)?;
        if unknown || size > end.saturating_sub(file.stream_position()?) {
            break;
        }
        let needed =
            matches!(id, 0x1549A966 | 0x1654AE6B | 0x1254C367) || (id == 0x1941A469 && cover);
        if needed && size <= 32 * 1024 * 1024 {
            let mut data = vec![0; size as usize];
            file.read_exact(&mut data)?;
            match id {
                0x1549A966 => info(&data, out),
                0x1654AE6B => out.codec = codec(&data),
                0x1254C367 => tags(&data, out),
                0x1941A469 if out.cover.is_none() => attachments(&data, out),
                _ => {}
            }
        } else {
            file.seek(SeekFrom::Current(
                i64::try_from(size).map_err(|_| io::ErrorKind::InvalidData)?,
            ))?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn element(id: u32, payload: &[u8]) -> Vec<u8> {
        let offset = (id.leading_zeros() / 8) as usize;
        let mut bytes = id.to_be_bytes()[offset..].to_vec();
        assert!(payload.len() < 127);
        bytes.push(0x80 | payload.len() as u8);
        bytes.extend(payload);
        bytes
    }

    fn tag(name: &str, value: &str) -> Vec<u8> {
        element(
            0x67C8,
            &[
                element(0x45A3, name.as_bytes()),
                element(0x4487, value.as_bytes()),
            ]
            .concat(),
        )
    }

    #[test]
    fn reads_metadata_after_audio_clusters() {
        let mut file = tempfile::tempfile().unwrap();
        let cover = b"\xff\xd8\xff\xe0\xff\xd9";
        let info = element(0x1549A966, &element(0x4489, &2000.0f64.to_be_bytes()));
        let cluster = element(0x1F43B675, &[0; 20]);
        let tags = element(
            0x1254C367,
            &element(
                0x7373,
                &[tag("TITLE", "Song"), tag("ARTIST", "Author")].concat(),
            ),
        );
        let image = element(
            0x1941A469,
            &element(
                0x61A7,
                &[element(0x4660, b"image/jpeg"), element(0x465C, cover)].concat(),
            ),
        );
        file.write_all(&element(0x1A45DFA3, b"")).unwrap();
        file.write_all(&element(0x18538067, &[info, cluster, tags, image].concat()))
            .unwrap();
        file.rewind().unwrap();
        let mut info = Metadata::default();
        read(&mut file, &mut info, true).unwrap();
        assert_eq!(info.title, "Song");
        assert_eq!(info.artists, ["Author"]);
        assert_eq!(info.duration_ms, 2000);
        assert_eq!(info.cover.as_deref(), Some(cover.as_slice()));
    }
}
